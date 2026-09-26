#include <base/resources.hpp>
#include <triolib.hpp>

#include <thread>
#include <resources/resource_pack.hpp>
#include <iostream>

using namespace std;
using namespace Mineclone;

struct ResourceHandlerImpl : ResourceHandler {
    virtual void start_loading() override {
        thread([this]() {
            cout << "Starting to load resources..." << endl;
            // start timer
            auto start = chrono::high_resolution_clock::now();
            has_started_loading = true;
            has_loaded = false;
            resources.whitelist.clear();
            // this->whitelist is unordered set
            for (const auto& folder : this->whitelist) {
                resources.whitelist.push_back(folder);
            }

            for (const auto& [origin, type] : resource_origins) {
                switch (type) {
                    case ResourceOriginType::ZipFromFs:
                        resources.load(Resources::ZipFromFs{origin});
                        break;
                    case ResourceOriginType::FolderFromFs:
                        resources.load(Resources::DirFromFs{origin});
                        break;
                    case ResourceOriginType::ZipFromCmrc:
                        resources.load(Resources::ZipFromEmbed{origin});
                        break;
                    case ResourceOriginType::FolderFromCmrc:
                        resources.load(Resources::DirFromEmbed{origin});
                        break;
                }
            }
            auto end = chrono::high_resolution_clock::now();
            cout << "Finished loading resources, time=" << chrono::duration_cast<chrono::milliseconds>(end - start).count() << "ms" << endl;
            has_loaded = true;
        }).detach();
    }
} REGISTER_MOD(ResourceHandlerImpl);
