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

package mobi.designmyapp.arpigl.listener;


/**
 * Equivalent interface of native class dma::geo::GeoEngineCallbacks.
 * This class provides a way to execute additional tasks on several Engine events.
 * IE : fetch resource from a data provider when the engine request a Tile or a Poi.
 */
public interface EngineListener {

    /**
     * Called each time the engine is missing resources for a tile. This happens
     * when the position moves on an area, and the needed png is not on the
     * storage to feed the tilemap.
     *
     * @param x coord of the requested tile.
     * @param y coord of the requested tile.
     * @param z coord of the requested tile.
     */
    void onTileRequest(int x, int y, int z);
}