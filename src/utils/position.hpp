#pragma once

#include <boost/multiprecision/cpp_int.hpp>

using BigInt = boost::multiprecision::cpp_int;

struct Position {
    float x = 0, y = 0, z = 0;

    static BigInt snap_distance;
    static float snap_distancef;

    BigInt offsetx = 0, offsety = 0, offsetz = 0;

    inline void snap() {
        auto norm = [&](float& a, BigInt& b) {
            while (a > snap_distancef) {
                a -= snap_distancef;
                b += snap_distance;
            }

            while (a < -snap_distancef) {
                a += snap_distancef;
                b -= snap_distance;
            }
        };

        norm(x, offsetx);
        norm(y, offsety);
        norm(z, offsetz);
    }

    inline Position operator+(const Position& other) const {
        Position result;

        result.x = x + other.x;
        result.y = y + other.y;
        result.z = z + other.z;

        result.offsetx = offsetx + other.offsetx;
        result.offsety = offsety + other.offsety;
        result.offsetz = offsetz + other.offsetz;

        result.snap();
        return result;
    }

    inline Position operator-(const Position& other) const {
        Position result;

        result.x = x - other.x;
        result.y = y - other.y;
        result.z = z - other.z;

        result.offsetx = offsetx - other.offsetx;
        result.offsety = offsety - other.offsety;
        result.offsetz = offsetz - other.offsetz;

        result.snap();
        return result;
    }
};

struct BigPos {
    BigInt x = 0, y = 0, z = 0;
};
