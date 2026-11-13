/**
 * @file world/server/worldgen/main.hpp
 * @brief The world generator interface
 * @author 60Trees_ (github.com/60Trees)
 * @detail
 * This file is responsible for defining
 * the interface for world generators.
 */

#pragma once

#include <world/shared.hpp>
#include <world/server/main.hpp>
#include <utils/position.hpp>
#include <triolib.hpp>

struct WorldGen : triolib::Module {
    tClass;

    virtual BigInt get_height_at(const BigInt& x, const BigInt& z) = 0;
    virtual Chunk generate_chunk(ChunkIndex&) = 0;
};
