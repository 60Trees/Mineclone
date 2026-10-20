Your message cut off after "2. Mesh /", so I've covered storage and meshing, plus the pieces of rendering and loading they depend on. If there was a third item, send it over.

## The core idea: a 3D clipmap pyramid

**Every LOD level uses the same chunk size (16³ voxels), but each voxel at level `L` covers `2^L` blocks per side.** A level-`L` chunk costs the same memory and mesh time as a level-0 chunk, but covers 8^L times the volume.

Each level keeps a window of `(2R+1)³` chunks around the camera (R ≈ 8). Because reach doubles per level, view distance is about `16·R·2^L` blocks. Cost is **O(log distance)**, better than the linear scaling you were aiming for.

| Levels | R | Reach | Chunks resident |
|---|---|---|---|
| 20 | 8 | ~134M blocks | ~100k (most are uniform and nearly free) |

Adding a level adds a constant cost, so you never run out of hardcoded levels. `MAX_LOD` is just an array size.

I use `lod 0` as full detail so that `v >> lod` gives voxel coordinates (your `lod 1` is my `lod 0`).

## 1. Storage

Replace `vector<unordered_map<Vec3i, Chunk>>` with a fixed array of levels holding `shared_ptr<Chunk>`. The pointers let workers hold a chunk after releasing the map lock, so nobody holds a global lock while meshing.

```cpp
constexpr int MAX_LOD    = 24;
constexpr int LOD_RADIUS = 8;   // half-width of each level's window, in chunks

// Your UnlockedChunk minus mesh/needs_remesh. Keep the single_tile trick:
// uniform chunks (sky, deep rock) cost ~nothing, which is what makes far levels cheap.
struct ChunkData { std::unique_ptr<RawChunk> raw; Tile single_tile; /* get/set/try_compress */ };

struct ChunkMesh { std::vector<BlockVertex> verts; };   // voxel-local coords 0..16

struct Chunk {
    std::shared_mutex mutex;                  // guards `data`
    ChunkData data;
    std::atomic<uint32_t> version{1};         // ++ on EVERY write
    std::atomic<uint32_t> meshed_version{0};
    std::atomic<bool> gen_queued{false}, mesh_queued{false};  // O(1) dedupe, replaces std::find on vectors

    std::mutex mesh_mutex;
    std::shared_ptr<const ChunkMesh> mesh;    // immutable once published; renderer just copies the pointer
};

struct Level {
    std::shared_mutex mutex;                  // held only for find/insert/erase
    std::unordered_map<Vec3i, std::shared_ptr<Chunk>, Vec3i::Vec3iHash> chunks;
    std::shared_ptr<Chunk> find(const Vec3i& p);
    std::shared_ptr<Chunk> get_or_create(const Vec3i& p);
    void erase(const Vec3i& p);
};

struct World {
    std::array<Level, MAX_LOD> levels;
    // ...
};
```

Cheap wins on the data itself:
- `Tile` is probably 8 bytes after padding, so a raw chunk is 32 KB. Use a per-chunk palette with 8-bit indices (4 KB) and widen only when needed.
- Use `>>`, not `/ pow(2,i)`. For negative ints, `>>` is a floor and `/` truncates toward zero, which is a bug in your current `get_chunk`.

```cpp
inline Vec3i shr(Vec3i v, int n) { return {v.x >> n, v.y >> n, v.z >> n}; }
inline int   cheb(Vec3i a, Vec3i b) { return std::max({abs(a.x-b.x), abs(a.y-b.y), abs(a.z-b.z)}); }
```

### Which chunks exist vs. which are drawn

- **Resident (data loaded):** all level-`L` chunks with `cheb(c, cam >> L) <= R`.
- **Drawn (meshed):** resident chunks that are *not* completely covered by finer detail.

```cpp
bool resident(int lod, Vec3i c, Vec3i cam_base) {
    return cheb(c, shr(cam_base, lod)) <= LOD_RADIUS;
}
bool drawn(int lod, Vec3i c, Vec3i cam_base) {
    if (!resident(lod, c, cam_base)) return false;
    if (lod == 0) return true;
    for (int i = 0; i < 8; ++i) {                       // all 8 children in the finer window? then it's a hole
        Vec3i child{c.x*2 + (i&1), c.y*2 + ((i>>1)&1), c.z*2 + ((i>>2)&1)};
        if (!resident(lod - 1, child, cam_base)) return true;
    }
    return false;
}
```

This tiles space exactly: no overlap (so no z-fighting) and no gaps. Coarse chunks inside a hole stay resident but unmeshed. That's about 15% overhead per level, and it keeps the mip pyramid intact for edits and for sampling across LOD borders.

The loader only needs to re-enumerate level `L` when `cam >> L` changes. That happens half as often with each level, so the work per level naturally halves (which is why your 3/2/1/1 thread split felt right).

### Generation and edits

- **Far chunks:** generate directly by sampling the terrain function at stride `2^lod`. Chunk sample count is constant, and chunks entirely above or below the height bounds take the `single_tile` fast path. This avoids needing fine chunks loaded.
- **Edits:** an edit at level 0 changes exactly one voxel per higher level, so the update is O(levels × 8):

```cpp
void World::set_block(int x, int y, int z, Tile t) {
    for (int lod = 0; lod < MAX_LOD; ++lod) {
        Vec3i v{x >> lod, y >> lod, z >> lod};                 // voxel at this level
        auto chunk = levels[lod].find(shr(v, 4));
        if (!chunk) break;
        Tile value = lod == 0 ? t : downsample_voxel(lod - 1, v);  // its 8 children, all in one chunk below
        if (!chunk->write(v.x & 15, v.y & 15, v.z & 15, value)) break;  // unchanged -> stop propagating
        // if on a chunk border, also bump the neighbour chunk's version so it remeshes
    }
}
```

`downsample_voxel` rule for lod ≥ 1: if any child is non-air, use the most common non-air child; otherwise air. Majority-vote makes thin features vanish with distance, and this rule keeps mountains slightly fat instead.

## 2. Meshing

### Replace "thread per LOD" with one priority pool

Each level has the same number of chunks, so a fixed thread count per level wastes threads on quiet levels and caps you at a fixed number of levels. Use one pool (N = cores − 2) with a priority queue keyed by distance to camera, so near and fine chunks go first. To stop distant levels being starved, reserve one thread that only takes `lod >= 2` jobs. This keeps your idea without needing a thread per level.

### Versioned mesh jobs

Your current loop clears `needs_remesh` even if the chunk changed mid-mesh, and its O(n·m) position matching is slow. Use versions instead:

```cpp
void mesh_one(const MeshJob& job) {            // {lod, pos}
    auto chunk = world->levels[job.lod].find(job.pos);
    if (!chunk) return;

    thread_local PaddedVoxels in;              // 18³, reused per thread
    uint32_t v;
    { std::shared_lock lk(chunk->mutex); v = chunk->version; in.load_center(chunk->data); }

    in.load_borders(*world, job.lod, job.pos); // 6 face slices via sample() below
    auto mesh = std::make_shared<ChunkMesh>();
    greedy_mesh(in, *mesh);

    if (chunk->version != v) { requeue(job); return; }   // edited meanwhile -> redo
    { std::lock_guard lk(chunk->mesh_mutex); chunk->mesh = std::move(mesh); chunk->meshed_version = v; }
}
```

Skip chunks whose center is uniform and whose six neighbors are uniform and opaque, since they produce no faces.

### Seams between LODs

One sampler handles same-level neighbors, unloaded neighbors, and LOD borders. It falls back to coarser levels:

```cpp
Tile sample(int lod, Vec3i voxel) {            // voxel coords at `lod`, any location
    for (int l = lod; l < MAX_LOD; ++l) {
        Vec3i v = shr(voxel, l - lod);
        if (auto c = levels[l].find(shr(v, 4))) { std::shared_lock lk(c->mutex); return c->data.get(v.x&15, v.y&15, v.z&15); }
    }
    return OPAQUE_UNKNOWN;                     // treat unknown as solid so you don't emit faces at the world edge
}
```

Fine chunks at the window edge sample the coarser level for culling, and coarse chunks next to a hole see the resident-but-undrawn data. Tiny cracks can still show at LOD borders. Fix them later with skirts (extend border faces downward by one voxel). They're cheap and fine for blocky terrain.

Also remesh neighbors when a chunk finishes generating, since they previously culled against "unknown."

### Mesh format

Vertices are chunk-local and unscaled (`uint8` coords 0..16 fit your `BlockVertex`). The LOD scale lives in a per-chunk transform, so the same mesher works for every level. Two caveats with greedy merging:

- Your atlas UVs are pixel coordinates, so merged quads would stretch the texture. Send atlas-origin and quad-local UV separately and use `fract()` in the fragment shader.
- At `lod >= 3`, skip textures and use a per-tile average color. It's faster and avoids shimmering.

## 3. Rendering (needed for this to actually scale)

Your renderer rebuilds and uploads every vertex every frame, and steals meshes out of chunks and puts them back. That can't scale past a few hundred chunks.

- Upload each chunk's `shared_ptr<const ChunkMesh>` once, when `meshed_version` changes. Keep GPU buffers in one big suballocated buffer and draw with `glMultiDrawArraysIndirect`.
- Per chunk, compute `origin = chunk_base_pos - camera` in **double**, then cast to float, and scale by `2^lod`. This keeps precision at huge distances.
- Use reversed-Z with an infinite far plane instead of `4096`.
- When a new chunk isn't meshed yet, keep drawing its coarser ancestor or finer descendants until the replacement is ready, so there are no holes while moving. Evict old meshes only after that.

## Bugs in the current code

- `world->chunks` is a vector of maps, but `worldgen`, the renderer, and `meshloop` all use it as a single map, so it won't compile.
- `get_lod` is declared as `function<int(const Vec3i&, int)>` but assigned a one-argument lambda.
- Worker threads hold `unique_lock(chunks_mutex)` while inserting, which stalls the renderer and mesher. The `shared_ptr<Chunk>` design fixes this.
- Nothing ever unloads chunks, and `Tile::data` is uninitialized.

The scheduling falls out cleanly if you change one thing from my earlier answer: replace the fixed window per level with a **refinement tree**. Start from a few huge root chunks and ask the policy, for each node, whether to **cull** it, **keep** it as a leaf, or **split** it into 8 children. That per-node decision is your `std::function`. Both of your algorithms become different lambdas over the same traversal, and the fixed clipmap is just the distance rule with a constant.

```
renderer ─► View ─► Planner thread ─► Selection (leaves) ─► JobBoard ─► worker lanes ─► chunk data/mesh
              (camera, fov,   │   (policy.select, policy.priority)                      │
               viewport)      └────────────────── draw list ◄───────────────────────────┘
```

## 1. The pluggable policy

```cpp
struct ChunkKey {
    int lod; Vec3i pos;                       // pos is in units of this level's chunks
    bool operator==(const ChunkKey&) const = default;
};
struct ChunkKeyHash {
    size_t operator()(const ChunkKey& k) const noexcept {
        return Vec3i::Vec3iHash::mix(Vec3i::Vec3iHash{}(k.pos) ^ uint64_t(k.lod));
    }
};

struct View {
    glm::dvec3 cam;
    std::array<glm::dvec4, 4> frustum;        // 4 side planes, world space, normal points inward
    double px_scale;                          // viewport_h / (2*tan(fov_y/2)); zooming in raises this
};

struct Aabb { glm::dvec3 min, max; };
struct NodeCtx { ChunkKey key; Aabb box; bool was_split; };   // was_split = hysteresis input

enum class Decision : uint8_t { Cull, Keep, Split };

using LodSelector = std::function<Decision(const NodeCtx&, const View&)>;
using PriorityFn  = std::function<float(const ChunkKey&, const View&)>;   // bigger = more urgent

struct LodPolicy { LodSelector select; PriorityFn priority; };
```

Helpers (the frustum test only uses the 4 side planes, which also works with reversed-Z and an infinite far plane):

```cpp
inline Aabb chunk_aabb(const ChunkKey& k) {
    double s = double(CHUNK_SIZE) * std::ldexp(1.0, k.lod);       // world size of this chunk
    glm::dvec3 mn{k.pos.x * s, k.pos.y * s, k.pos.z * s};
    return {mn, mn + glm::dvec3(s)};
}
inline double size_of(const Aabb& b) { return b.max.x - b.min.x; }

inline double dist_to_aabb(const Aabb& b, const glm::dvec3& p) {
    return glm::length(glm::max(glm::max(b.min - p, p - b.max), glm::dvec3(0)));
}
inline bool in_frustum(const Aabb& b, const View& v) {
    for (auto& pl : v.frustum) {
        glm::dvec3 p{pl.x >= 0 ? b.max.x : b.min.x, pl.y >= 0 ? b.max.y : b.min.y, pl.z >= 0 ? b.max.z : b.min.z};
        if (glm::dot(glm::dvec3(pl), p) + pl.w < 0) return false;
    }
    return true;
}
// Gribb-Hartmann; `m` = world->clip matrix built in double
inline std::array<glm::dvec4, 4> side_planes(const glm::dmat4& m) {
    auto r = [&](int i) { return glm::dvec4(m[0][i], m[1][i], m[2][i], m[3][i]); };
    return {r(3) + r(0), r(3) - r(0), r(3) + r(1), r(3) - r(1)};
}
```

## 2. Traversal (shared by every policy)

```cpp
struct Selection {
    std::vector<ChunkKey> leaves;
    std::unordered_set<ChunkKey, ChunkKeyHash> split;    // interior nodes
};

void select_rec(const ChunkKey& key, const View& v, const LodPolicy& pol,
                const std::unordered_set<ChunkKey, ChunkKeyHash>& prev_split, Selection& out) {
    NodeCtx n{key, chunk_aabb(key), prev_split.contains(key)};
    Decision d = pol.select(n, v);
    if (key.lod == 0 && d == Decision::Split) d = Decision::Keep;
    if (d == Decision::Cull) return;
    if (d == Decision::Keep) { out.leaves.push_back(key); return; }
    out.split.insert(key);
    for (int i = 0; i < 8; ++i)
        select_rec({key.lod - 1, {key.pos.x * 2 + (i & 1), key.pos.y * 2 + ((i >> 1) & 1), key.pos.z * 2 + ((i >> 2) & 1)}},
                   v, pol, prev_split, out);
}

Selection select(const View& v, const LodPolicy& pol, const auto& prev_split) {
    Selection out;
    const int top = MAX_LOD - 1;
    const double s = CHUNK_SIZE * std::ldexp(1.0, top);
    Vec3i c{int(std::floor(v.cam.x / s)), int(std::floor(v.cam.y / s)), int(std::floor(v.cam.z / s))};
    for (int x = -1; x <= 1; ++x) for (int y = -1; y <= 1; ++y) for (int z = -1; z <= 1; ++z)
        select_rec({top, {c.x + x, c.y + y, c.z + z}}, v, pol, prev_split, out);
    return out;
}
```

Each level only visits children of split parents, so the visit count is a few thousand per level at most, and culling cuts that further. Run it on the planner thread at roughly 20 Hz, not per frame.

## 3. The two policies

**Simple** splits when the camera is within `k` chunk-sizes of the node's nearest point:

```cpp
LodPolicy make_simple_policy(double k = 4.0) {
    return {
        [k](const NodeCtx& n, const View& v) {
            double size = size_of(n.box), d = dist_to_aabb(n.box, v.cam);
            if (d > 2 * size && !in_frustum(n.box, v)) return Decision::Cull;   // never cull what's near you
            double thr = k * size * (n.was_split ? 1.15 : 1.0);                 // hysteresis: harder to merge
            return d < thr ? Decision::Split : Decision::Keep;
        },
        [](const ChunkKey& key, const View& v) { return float(-dist_to_aabb(chunk_aabb(key), v.cam)); }
    };
}
```

**Detailed (pixel-based)** splits when one voxel of this node would cover more than `target_px` pixels:

```cpp
struct PixelParams { std::atomic<float> target_px{10.0f}; float merge_ratio = 0.7f; };

LodPolicy make_pixel_policy(std::shared_ptr<PixelParams> p) {
    return {
        [p](const NodeCtx& n, const View& v) {
            double size = size_of(n.box), d = std::max(dist_to_aabb(n.box, v.cam), 1e-3);
            if (d > 2 * size && !in_frustum(n.box, v)) return Decision::Cull;
            double voxel_px = (size / CHUNK_SIZE) * v.px_scale / d;
            double t = p->target_px * (n.was_split ? p->merge_ratio : 1.0);
            return voxel_px > t ? Decision::Split : Decision::Keep;
        },
        [](const ChunkKey& key, const View& v) {              // projected screen area
            Aabb b = chunk_aabb(key);
            double px = size_of(b) * v.px_scale / std::max(dist_to_aabb(b, v.cam), 1e-3);
            return float(px * px * (in_frustum(b, v) ? 1.0 : 0.1));
        }
    };
}
```

Zooming just changes `px_scale`, so the same code gives more detail up close and a narrower frustum. Switching algorithms is one call: `planner.set_policy(make_pixel_policy(params))`.

### Cost

Per level, the leaf count is roughly `(4K)³ × (frustum fraction)` where `K = px_scale / (16 · target_px)`. At 1080p with a 70° FOV, `target_px = 12` gives K≈4 (~4k chunks per level before culling). `target_px = 4` is **27× more**. With a fixed `target_px`, zooming 2× cuts the frustum area 4× but doubles K, so the chunk count roughly *doubles* rather than shrinking. To keep the "linear lag" promise, add a budget controller that adjusts the threshold:

```cpp
void tune(PixelParams& p, size_t leaves, size_t budget) {
    float f = float(leaves) / float(budget);
    if (f > 1.0f)      p.target_px = std::min(p.target_px * 1.05f, 64.0f);   // too many chunks -> coarser
    else if (f < 0.7f) p.target_px = std::max(p.target_px * 0.98f, 1.0f);    // headroom -> finer
}
```

## 4. Job board and worker lanes

**Jobs are derived from state, not stored as events.** Every planner pass rebuilds the pending list from the current leaves. This means priorities are never stale, and chunks that drop out of view cancel for free.

```cpp
enum class JobKind : uint8_t { Generate, Mesh };
struct Job { ChunkKey key; JobKind kind; float priority; };
struct Lane { int lod_min, lod_max; };

class JobBoard {
public:
    void publish(std::vector<Job> jobs) {                 // replaces everything pending
        std::lock_guard lk(m);
        for (auto& b : buckets) b.clear();
        for (auto& j : jobs) buckets[j.key.lod].push_back(j);
        for (auto& b : buckets)                           // ascending: best job is back()
            std::sort(b.begin(), b.end(), [](auto& a, auto& c) { return a.priority < c.priority; });
        cv.notify_all();
    }
    std::optional<Job> pop_wait(Lane lane) {
        std::unique_lock lk(m);
        cv.wait_for(lk, std::chrono::milliseconds(100), [&] { return stop || any(); });
        if (stop) return std::nullopt;
        int best = pick(lane.lod_min, lane.lod_max);      // prefer own lane
        if (best < 0) best = pick(0, MAX_LOD - 1);        // otherwise steal from anyone
        if (best < 0) return std::nullopt;
        Job j = buckets[best].back(); buckets[best].pop_back();
        return j;
    }
    bool stop = false;
private:
    int pick(int lo, int hi) {
        int best = -1;
        for (int l = lo; l <= hi; ++l)
            if (!buckets[l].empty() && (best < 0 || buckets[l].back().priority > buckets[best].back().priority)) best = l;
        return best;
    }
    bool any() { for (auto& b : buckets) if (!b.empty()) return true; return false; }
    std::mutex m; std::condition_variable cv;
    std::array<std::vector<Job>, MAX_LOD> buckets;
};
```

The lanes keep your "3 threads fine, 2 mid, 1 coarse" idea without wasting idle threads, because a worker with no jobs in its lane steals from the others:

```cpp
const Lane lanes[] = {{0,1},{0,1},{0,1},{2,3},{2,3},{4,MAX_LOD-1}};   // one worker thread per entry

void worker_main(Lane lane) {
    while (running) {
        auto job = board.pop_wait(lane);
        if (!job) continue;
        auto chunk = world->levels[job->key.lod].find(job->key.pos);
        if (!chunk || chunk->busy.exchange(true)) continue;      // O(1) in-flight dedupe
        if (job->kind == JobKind::Generate) { generate(*chunk, job->key); chunk->generated = true; }
        else                                { mesh_one(*job); }
        chunk->busy = false;
        planner_poke();                                          // wake planner: new data may unlock meshing / swaps
    }
}
```

`Chunk` gains `std::atomic<bool> busy, generated; std::atomic<uint32_t> last_wanted;`. The lane split also guards against starvation, since near-field churn can never use up the coarse lane's thread.

## 5. Planner loop, hole-free swaps, eviction

```cpp
std::vector<Job> derive_jobs(const Selection& sel, const View& v, const LodPolicy& pol, uint32_t epoch) {
    std::vector<Job> jobs;
    for (auto& key : sel.leaves) {
        auto chunk = world->levels[key.lod].get_or_create(key.pos);   // empty shell is cheap
        chunk->last_wanted = epoch;
        float prio = pol.priority(key, v);
        if (!chunk->generated)                                  jobs.push_back({key, JobKind::Generate, prio});
        else if (chunk->meshed_version != chunk->version)       jobs.push_back({key, JobKind::Mesh,     prio});
    }
    return jobs;
}

void planner_main() {
    while (running) {
        View v = snapshot_view();                               // renderer writes this each frame
        if (view_changed(v, last_view) || poked.exchange(false)) {
            auto pol = policy_snapshot();                       // shared_ptr<const LodPolicy>, swappable at runtime
            Selection sel = select(v, *pol, prev_split);
            ++epoch;
            board.publish(derive_jobs(sel, v, *pol, epoch));
            publish_draw_list(resolve_draw_list(sel));
            if (pixel_params) tune(*pixel_params, sel.leaves.size(), leaf_budget);
            prev_split = std::move(sel.split); last_view = v;
            if (epoch % 64 == 0) evict(epoch);
        }
        wait_for_poke(std::chrono::milliseconds(50));
    }
}
```

Eviction is just "not wanted for a while." The grace period also gives you zoom hysteresis for free, so zooming out and back in doesn't regenerate everything:

```cpp
void evict(uint32_t epoch, uint32_t grace = 200) {
    for (auto& lvl : world->levels) {
        std::unique_lock lk(lvl.mutex);
        for (auto it = lvl.chunks.begin(); it != lvl.chunks.end();)
            it = (epoch - it->second->last_wanted > grace && !it->second->busy) ? lvl.chunks.erase(it) : std::next(it);
    }                                                           // shared_ptr keeps meshes alive for the GPU/renderer
}
```

To avoid pop-in or holes, a split node is only drawn as its children once **all** of them are ready. Until then, keep drawing the parent (still within its eviction grace):

```cpp
bool resolve(const ChunkKey& key, const Selection& sel, std::vector<ChunkKey>& draw) {
    if (!sel.split.contains(key)) return ready(key) ? (draw.push_back(key), true) : false;  // leaf
    size_t mark = draw.size();
    bool ok = true;
    for (auto& c : children(key)) {
        if (!sel.split.contains(c) && !in_leaves(sel, c)) continue;   // culled child: nothing to draw
        ok &= resolve(c, sel, draw);
    }
    if (ok) return true;
    draw.resize(mark);                                          // roll back the partial children
    return ready(key) ? (draw.push_back(key), true) : false;    // fall back to this node's own old mesh
}
```

Boost the priority of any leaf where `resolve` fails with no fallback (a real hole on screen) above everything else. That one rule makes the scheduler respond sensibly to fast camera movement.

## How it maps to your code

- `WorldLoader` becomes the planner. It replaces `get_lod` and `render_distance` with a `LodPolicy`.
- `WorldGen`'s per-LOD threads and `ChunkMeshLoop`'s scan loop become the single worker pool above.
- `TestRenderer` fills `View` each frame (`px_scale = height / (2·tan(fov/2))`) and draws the planner's draw list.
- For deep levels, the policies' `d > 2*size` cull margin is deliberately generous. If you still see void when turning fast, widen the frustum planes by 20–30° when building `View`.
- With nearest-point distance, neighbouring leaves differ by at most about one level. The skirts from before cover the remaining cracks, so you don't need a separate 2:1 balancing pass.
