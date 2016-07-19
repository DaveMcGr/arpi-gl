/*
 * Copyright (C) 2015  eBusiness Information
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */



#include <utils/GeoUtils.hpp>
#include <utils/ExceptionUtils.hpp>
#include <geo/tile/mvt/vector_tile.hpp>
#include <shape/ShapeFactory.hpp>
#include <shape/GeometryUtils.hpp>
#include <geo/tile/mvt/GeometryMapper.hpp>
#include "geo/tile/TileMap.hpp"
#include "geo/tile/Tile.hpp"
#include "geo/GeoSceneManager.hpp"

#define DEFAULT_TILE_DIFFUSE_MAP "damier"

constexpr auto TAG = "TileMap";
constexpr int SIZE = 13;
constexpr int OFFSET = SIZE / 2;


namespace dma {

    //constexpr int TileMap::ZOOM;


    bool TileMap::isInRange(int x, int y, int x0, int y0) {
        return x >= x0 - OFFSET
               && x <= x0 + OFFSET
               && y >= y0 - OFFSET
               && y <= y0 + OFFSET;
    }

    /****************************************************************************/
    /*                                 MEMBERS                                  */
    /****************************************************************************/

    TileMap::TileMap(ResourceManager& resourceManager, GeoSceneManager& geoSceneManager) :
            mGeoSceneManager(geoSceneManager),
            mResourceManager(resourceManager),
            mTileFactory(resourceManager),
            mLastX(-1),
            mLastY(-1),
            mNullCallbacks(new GeoEngineCallbacks()),
            mCallbacks(mNullCallbacks) {

    }

    TileMap::~TileMap() {
        unload();
        delete mNullCallbacks;
    }

    void TileMap::init() {
        mLastX = mLastY = -1;
        // Create TILE_MAP_SIZE * TILE_MAP_SIZE tiles
        for (int i = 0; i < SIZE * SIZE; ++i) {
            mTiles.push_back(std::make_shared<Tile>());
        }
    }

    void TileMap::unload() {
        mRemoveAllTiles();
        mLastX = mLastY = -1;
    }

    void TileMap::update(int x0, int y0) {
        Log::trace(TAG, "Updating TileMap (%d, %d, %d)", x0, y0, ZOOM);

        //TODO check x and y bounds
        if (x0 <= OFFSET || y0 <= OFFSET) {
            return;
        }

        int z = ZOOM;
        // if update gives the same tile : skip.
        if (x0 == mLastX && y0 == mLastY) {
            return;
        }

        std::list<std::shared_ptr<Tile>> toUpdate;

        for (auto tile : mTiles) {
            if (!isInRange(tile->x, tile->y, x0, y0)) {
                toUpdate.push_back(tile);
            }
        }

        for (int x = x0 - OFFSET; x <= x0 + OFFSET; ++x) {
            for (int y = y0 - OFFSET; y <= y0 + OFFSET; ++y) {
                if (!isInRange(x, y, mLastX, mLastY)) {
                    updateTile(toUpdate.front(), x, y, z);
                    toUpdate.pop_front();
                }
            }
        }

        //mResourceManager.prune(); // unload unused resources TODO removed for buildings / tracks demo

        mLastX = x0;
        mLastY = y0;
    }

    Status TileMap::notifyTileAvailable(int x, int y, int z) {
        Log::trace(TAG, "Notifying tile available (%d, %d, %d)", x, y, z);
        std::shared_ptr<Tile> tile = findTile(x, y, z);
        if (tile == nullptr) {
            std::stringstream ss;
            ss << "Trying to set Tile Image but Tile (" << x << ", " << y << ", " << z << ") doesn't exist in the TileMap";
            Log::error(TAG, "%", ss.str().c_str());
            return STATUS_KO;
            //throw std::runtime_error(ss.str());
            //throwException(TAG, ExceptionType::NO_SUCH_ELEMENT, ss.str());
        }
        std::string sid = tileSid(x, y, z);
        std::shared_ptr<Map> diffuseMap = mResourceManager.acquireMap(sid);
        //tile->setDiffuseMap(diffuseMap);
        return STATUS_OK;
    }

    //TODO use a tile pool
    void TileMap::updateTile(std::shared_ptr<Tile> tile, int x, int y, int z) {

        for (auto ge : tile->mGeoEntities) {
            mGeoSceneManager.removeGeoEntity(ge);
        }

        tile->xyz(x, y, z);

        for (auto& layer : mStyle.getLayers()) {
            switch (layer.getType()) {
                case Layer::Type::EXTRUDE: {
                    const auto &source = mStyle.getSources().at(layer.getSource());
                    auto tileData = source.fetch(x, y, z);
                    if (!tileData.empty()) {
                        vector_tile::Tile vectorTile;
                        vectorTile.ParseFromArray(tileData.data(), (int) tileData.size());
                        for (auto& f : vectorTile.layers(0).features()) {
                            std::vector<Polygon> polygons = GeometryMapper::polygons(f.geometry());

                            float scaleX = tile->mWidth / vectorTile.layers(0).extent();
                            float scaleY = tile->mHeight / vectorTile.layers(0).extent();
                            for (auto& p : polygons) {
                                GeometryUtils::scale(p, scaleX, scaleY);
                            }

                            ShapeFactory shapeFactory(mResourceManager);

                            auto mesh = shapeFactory.polygon(polygons[0]);
                            auto material = mResourceManager.acquireMaterial("building");
                            auto building = std::make_shared<GeoEntity>(mesh, material);

                            building->setCoords(tile->mCoords);

                            tile->mGeoEntities.push_back(building);
                        }
                    }

                    break;
                }
                case Layer::Type::BACKGROUND:
                    break;
                case Layer::Type::FILL:
                    break;
                case Layer::Type::LINE:
                    break;
                case Layer::Type::SYMBOL:
                    break;
                case Layer::Type::RASTER: {
                    std::shared_ptr<Quad> quad = mResourceManager.createQuad(tile->mWidth, tile->mHeight);
                    auto material = mResourceManager.createMaterial("tile");
                    auto rasterTile = std::make_shared<GeoEntity>(quad, material);
                    rasterTile->pitch(-90.0f);
                    rasterTile->setScale(quad->getScale());

                    rasterTile->setCoords(tile->mCoords);

                    // Shifts the quad position since its origin is the center
                    glm::vec3 pos = rasterTile->getPosition();
                    pos.x += tile->mWidth / 2.0f;
                    pos.z += tile->mHeight / 2.0f;
                    rasterTile->setPosition(pos);

                    tile->mGeoEntities.push_back(rasterTile);

                    const auto& source = mStyle.getSources().at(layer.getSource());
                    auto tileData = source.fetch(x, y, z);
                    if (!tileData.empty()) {
                        Image image;
                        image.loadAsPNG(tileData);
                        auto diffuseMap = mResourceManager.createMap(image);
                        material->setDiffuseMap(diffuseMap, 0);
                    } else {
                        material->setDiffuseMap(mResourceManager.acquireMap(DEFAULT_TILE_DIFFUSE_MAP), 0);
                    }
                    break;
                }
                case Layer::Type::CIRCLE:
                    break;
                default:
                    ExceptionUtils::runtime(TAG, "Unknown layer type: " + layer.getType());
            }
        }

        for (auto ge : tile->mGeoEntities) {
            mGeoSceneManager.addGeoEntity(ge);
        }

        //Log::trace(TAG, "Tile (%d, %d, %d) updated, diffuse map: %s", x, y, z, diffuseMap->getSID().c_str());
    }

    void TileMap::mRemoveAllTiles() {
        for (auto t : mTiles) {
            for (auto ge : t->mGeoEntities) {
                mGeoSceneManager.removeGeoEntity(ge);
            }
        }
        mTiles.clear();
    }

    std::shared_ptr<Tile> TileMap::findTile(int x, int y, int z) {
        for (auto tile : mTiles) {
            if (tile->x == x && tile->y == y && tile->z == z) {
                return tile;
            }
        }
        return nullptr;
    }

    std::string TileMap::tileSid(int x, int y, int z) const {
        std::stringstream ssid;
        ssid << "tiles/";
        if (!mNamespace.empty()) {
            ssid << mNamespace << "/";
        }
        ssid << z << "/" << x << "/" << y;
        return std::move(ssid.str());
    }

    void TileMap::setNamespace(const std::string &ns) {
        Log::debug(TAG, "Setting namespace: %s", ns.c_str());
        mNamespace = ns;
        if (mTiles.front()->x != -1) { // -1 means tile map not set
            updateDiffuseMaps();
        }
    }

    void TileMap::updateDiffuseMaps() {
        for (std::shared_ptr<Tile> tile : mTiles) {
            std::string sid = tileSid(tile->x, tile->y, tile->z);
            if (mResourceManager.hasMap(sid)) {
                //TODO tile->setDiffuseMap(mResourceManager.acquireMap(sid));
            } else {
                //TODO tile->setDiffuseMap(mResourceManager.acquireMap(DEFAULT_TILE_DIFFUSE_MAP));
                mCallbacks->onTileRequest(tile->x, tile->y, tile->z);
            }
        }
    }

    void TileMap::setStyle(const Style &style) {
        mStyle = style;
        for (auto& t : mTiles) {
            updateTile(t, t->x, t->y, t->z);
        }
    }


}
