/* $Id$ */

/** @file order_gui.cpp */

#include "stdafx.h"
#include "openttd.h"
#include "road_map.h"
#include "station_map.h"
#include "gui.h"
#include "window_gui.h"
#include "station_base.h"
#include "town.h"
#include "command_func.h"
#include "viewport_func.h"
#include "gfx_func.h"
#include "depot.h"
#include "waypoint.h"
#include "train.h"
#include "water_map.h"
#include "vehicle_gui.h"
#include "timetable.h"
#include "cargotype.h"
#include "strings_func.h"
#include "window_func.h"
#include "vehicle_func.h"
#include "settings_type.h"
#include "player_func.h"
#include "newgrf_cargo.h"
#include "widgets/dropdown_func.h"

#include "table/sprites.h"
#include "table/strings.h"

enum OrderWindowWidgets {
	ORDER_WIDGET_CLOSEBOX = 0,
	ORDER_WIDGET_CAPTION,
	ORDER_WIDGET_TIMETABLE_VIEW,
	ORDER_WIDGET_ORDER_LIST,
	ORDER_WIDGET_SCROLLBAR,
	ORDER_WIDGET_SKIP,
	ORDER_WIDGET_DELETE,
	ORDER_WIDGET_NON_STOP,
	ORDER_WIDGET_GOTO,
	ORDER_WIDGET_FULL_LOAD,
	ORDER_WIDGET_UNLOAD,
	ORDER_WIDGET_REFIT,
	ORDER_WIDGET_TRANSFER,
	ORDER_WIDGET_SERVICE,
	ORDER_WIDGET_RESIZE_BAR,
	ORDER_WIDGET_SHARED_ORDER_LIST,
	ORDER_WIDGET_RESIZE,
};

/**
 * Return the memorised selected order.
 *
 * @param w current window
 * @return the memorised order if it is a vaild one
 *  else return the number of orders
 */
static int OrderGetSel(const Window *w)
{
	const Vehicle *v = GetVehicle(w->window_number);
	int num = WP(w, order_d).sel;

	return (num >= 0 && num < v->num_orders) ? num : v->num_orders;
}

/**
 * Calculate the selected order.
 * The calculation is based on the relative (to the window) y click position and
 *  the position of the scrollbar.
 *
 * @param w current window
 * @param y Y-value of the click relative to the window origin
 * @param v current vehicle
 * @return the new selected order if the order is valid else return that
 *  an invalid one has been selected.
 */
static int GetOrderFromOrderWndPt(Window *w, int y, const Vehicle *v)
{
	/*
	 * Calculation description:
	 * 15 = 14 (w->widget[ORDER_WIDGET_ORDER_LIST].top) + 1 (frame-line)
	 * 10 = order text hight
	 */
	int sel = (y - 15) / 10;

	if ((uint)sel >= w->vscroll.cap) return INVALID_ORDER;

	sel += w->vscroll.pos;

	return (sel <= v->num_orders && sel >= 0) ? sel : INVALID_ORDER;
}

/** Order load types that could be given to station orders. */
static const StringID _station_load_types[][5] = {
	{
		STR_EMPTY,
		STR_ORDER_UNLOAD,
		STR_ORDER_FULL_LOAD,
		STR_ORDER_FULL_LOAD_ANY,
		STR_ORDER_NO_LOAD,
	}, {
		STR_ORDER_TRANSFER,
		STR_ORDER_TRANSFER_UNLOAD,
		STR_ORDER_TRANSFER_FULL_LOAD,
		STR_ORDER_TRANSFER_FULL_LOAD_ANY,
		INVALID_STRING_ID,
	}
};

static void DrawOrdersWindow(Window *w)
{
	const Vehicle *v = GetVehicle(w->window_number);
	bool shared_orders = v->IsOrderListShared();

	SetVScrollCount(w, v->num_orders + 1);

	int sel = OrderGetSel(w);
	const Order *order = GetVehicleOrder(v, sel);

	if (v->owner == _local_player) {
		/* skip */
		w->SetWidgetDisabledState(ORDER_WIDGET_SKIP, v->num_orders <= 1);

		/* delete */
		w->SetWidgetDisabledState(ORDER_WIDGET_DELETE,
				(uint)v->num_orders + ((shared_orders || v->num_orders != 0) ? 1 : 0) <= (uint)WP(w, order_d).sel);

		/* non-stop only for trains */
		w->SetWidgetDisabledState(ORDER_WIDGET_NON_STOP,  v->type != VEH_TRAIN || order == NULL);
		w->SetWidgetDisabledState(ORDER_WIDGET_FULL_LOAD, order == NULL); // full load
		w->SetWidgetDisabledState(ORDER_WIDGET_UNLOAD,    order == NULL); // unload
		w->SetWidgetDisabledState(ORDER_WIDGET_TRANSFER,  order == NULL); // transfer
		/* Disable list of vehicles with the same shared orders if there is no list */
		w->SetWidgetDisabledState(ORDER_WIDGET_SHARED_ORDER_LIST, !shared_orders || v->orders == NULL);
		w->SetWidgetDisabledState(ORDER_WIDGET_REFIT,     order == NULL); // Refit
		w->SetWidgetDisabledState(ORDER_WIDGET_SERVICE,   order == NULL); // Refit
		w->HideWidget(ORDER_WIDGET_REFIT); // Refit
		w->HideWidget(ORDER_WIDGET_SERVICE); // Service
	} else {
		w->DisableWidget(ORDER_WIDGET_TRANSFER);
		w->DisableWidget(ORDER_WIDGET_FULL_LOAD);
	}

	w->ShowWidget(ORDER_WIDGET_UNLOAD);
	w->ShowWidget(ORDER_WIDGET_TRANSFER);

	if (order != NULL) {
		switch (order->GetType()) {
			case OT_GOTO_STATION:
				if (!GetStation(order->GetDestination())->IsBuoy()) break;
				/* Fall-through */

			case OT_GOTO_WAYPOINT:
				w->DisableWidget(ORDER_WIDGET_FULL_LOAD);
				w->DisableWidget(ORDER_WIDGET_UNLOAD);
				w->DisableWidget(ORDER_WIDGET_TRANSFER);
				break;

			case OT_GOTO_DEPOT:
				w->DisableWidget(ORDER_WIDGET_FULL_LOAD);

				/* Remove unload and replace it with refit */
				w->HideWidget(ORDER_WIDGET_UNLOAD);
				w->ShowWidget(ORDER_WIDGET_REFIT);
				w->HideWidget(ORDER_WIDGET_TRANSFER);
				w->ShowWidget(ORDER_WIDGET_SERVICE);
				break;

			default: // every other orders
				w->DisableWidget(ORDER_WIDGET_NON_STOP);
				w->DisableWidget(ORDER_WIDGET_FULL_LOAD);
				w->DisableWidget(ORDER_WIDGET_UNLOAD);
		}
	}

	SetDParam(0, v->index);
	DrawWindowWidgets(w);

	int y = 15;

	int i = w->vscroll.pos;
	order = GetVehicleOrder(v, i);
	StringID str;
	while (order != NULL) {
		str = (v->cur_order_index == i) ? STR_8805 : STR_8804;
		SetDParam(5, STR_EMPTY);

		if (i - w->vscroll.pos < w->vscroll.cap) {
			SetDParam(1, 6);

			switch (order->GetType()) {
				case OT_DUMMY:
					SetDParam(1, STR_INVALID_ORDER);
					SetDParam(2, order->GetDestination());
					break;

				case OT_GOTO_STATION: {
					OrderLoadFlags load = order->GetLoadType();
					OrderUnloadFlags unload = order->GetUnloadType();

					SetDParam(1, STR_GO_TO_STATION);
					SetDParam(2, STR_ORDER_GO_TO + (v->type == VEH_TRAIN ? order->GetNonStopType() : 0));
					SetDParam(3, order->GetDestination());
					SetDParam(4, _station_load_types[unload >> 1][load | (unload & ~(OUFB_TRANSFER | OUFB_NO_UNLOAD))]);
				} break;

				case OT_GOTO_DEPOT: {
					StringID s = STR_NULL;

					if (v->type == VEH_AIRCRAFT) {
						s = STR_GO_TO_AIRPORT_HANGAR;
						SetDParam(2, order->GetDestination());
					} else {
						SetDParam(2, GetDepot(order->GetDestination())->town_index);

						switch (v->type) {
							case VEH_TRAIN: s = (order->GetNonStopType() & ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS) ? STR_880F_GO_NON_STOP_TO_TRAIN_DEPOT : STR_GO_TO_TRAIN_DEPOT; break;
							case VEH_ROAD:  s = STR_GO_TO_ROADVEH_DEPOT; break;
							case VEH_SHIP:  s = STR_GO_TO_SHIP_DEPOT; break;
							default: break;
						}
					}

					if (order->GetDepotOrderType() & ODTFB_SERVICE) s++; // service at

					SetDParam(1, s);
					if (order->IsRefit()) {
						SetDParam(5, STR_REFIT_ORDER);
						SetDParam(6, GetCargo(order->GetRefitCargo())->name);
					}
					break;
				}

				case OT_GOTO_WAYPOINT:
					SetDParam(1, (order->GetNonStopType() & ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS) ? STR_GO_NON_STOP_TO_WAYPOINT : STR_GO_TO_WAYPOINT);
					SetDParam(2, order->GetDestination());
					break;

				default: break;
			}

			SetDParam(0, i + 1);
			DrawString(2, y, str, (i == WP(w, order_d).sel) ? TC_WHITE : TC_BLACK);

			y += 10;
		}

		i++;
		order = order->next;
	}

	if (i - w->vscroll.pos < w->vscroll.cap) {
		str = shared_orders ? STR_END_OF_SHARED_ORDERS : STR_882A_END_OF_ORDERS;
		DrawString(2, y, str, (i == WP(w, order_d).sel) ? TC_WHITE : TC_BLACK);
	}
}

static Order GetOrderCmdFromTile(const Vehicle *v, TileIndex tile)
{
	Order order;
	order.next  = NULL;
	order.index = 0;

	/* check depot first */
	if (_patches.gotodepot) {
		switch (GetTileType(tile)) {
			case MP_RAILWAY:
				if (v->type == VEH_TRAIN && IsTileOwner(tile, _local_player)) {
					if (IsRailDepot(tile)) {
						order.MakeGoToDepot(GetDepotByTile(tile)->index, ODTFB_PART_OF_ORDERS);
						return order;
					}
				}
				break;

			case MP_ROAD:
				if (IsRoadDepot(tile) && v->type == VEH_ROAD && IsTileOwner(tile, _local_player)) {
					order.MakeGoToDepot(GetDepotByTile(tile)->index, ODTFB_PART_OF_ORDERS);
					return order;
				}
				break;

			case MP_STATION:
				if (v->type != VEH_AIRCRAFT) break;
				if (IsHangar(tile) && IsTileOwner(tile, _local_player)) {
					order.MakeGoToDepot(GetStationIndex(tile), ODTFB_PART_OF_ORDERS);
					return order;
				}
				break;

			case MP_WATER:
				if (v->type != VEH_SHIP) break;
				if (IsTileDepotType(tile, TRANSPORT_WATER) &&
						IsTileOwner(tile, _local_player)) {
					TileIndex tile2 = GetOtherShipDepotTile(tile);

					order.MakeGoToDepot(GetDepotByTile(tile < tile2 ? tile : tile2)->index, ODTFB_PART_OF_ORDERS);
					return order;
				}

			default:
				break;
		}
	}

	/* check waypoint */
	if (IsTileType(tile, MP_RAILWAY) &&
			v->type == VEH_TRAIN &&
			IsTileOwner(tile, _local_player) &&
			IsRailWaypoint(tile)) {
		order.MakeGoToWaypoint(GetWaypointByTile(tile)->index);
		return order;
	}

	if (IsTileType(tile, MP_STATION)) {
		StationID st_index = GetStationIndex(tile);
		const Station *st = GetStation(st_index);

		if (st->owner == _current_player || st->owner == OWNER_NONE) {
			byte facil;
			(facil = FACIL_DOCK, v->type == VEH_SHIP) ||
			(facil = FACIL_TRAIN, v->type == VEH_TRAIN) ||
			(facil = FACIL_AIRPORT, v->type == VEH_AIRCRAFT) ||
			(facil = FACIL_BUS_STOP, v->type == VEH_ROAD && IsCargoInClass(v->cargo_type, CC_PASSENGERS)) ||
			(facil = FACIL_TRUCK_STOP, 1);
			if (st->facilities & facil) {
				order.MakeGoToStation(st_index);
				if (_patches.new_nonstop && v->type == VEH_TRAIN) order.SetNonStopType(ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS);
				return order;
			}
		}
	}

	/* not found */
	order.Free();
	return order;
}

static bool HandleOrderVehClick(const Vehicle *v, const Vehicle *u, Window *w)
{
	if (u->type != v->type) return false;

	if (!u->IsPrimaryVehicle()) {
		u = u->First();
		if (!u->IsPrimaryVehicle()) return false;
	}

	/* v is vehicle getting orders. Only copy/clone orders if vehicle doesn't have any orders yet
	 * obviously if you press CTRL on a non-empty orders vehicle you know what you are doing */
	if (v->num_orders != 0 && _ctrl_pressed == 0) return false;

	if (DoCommandP(v->tile, v->index | (u->index << 16), _ctrl_pressed ? CO_SHARE : CO_COPY, NULL,
		_ctrl_pressed ? CMD_CLONE_ORDER | CMD_MSG(STR_CANT_SHARE_ORDER_LIST) : CMD_CLONE_ORDER | CMD_MSG(STR_CANT_COPY_ORDER_LIST))) {
		WP(w, order_d).sel = -1;
		ResetObjectToPlace();
	}

	return true;
}

static void OrdersPlaceObj(const Vehicle *v, TileIndex tile, Window *w)
{
	/* check if we're clicking on a vehicle first.. clone orders in that case. */
	const Vehicle *u = CheckMouseOverVehicle();
	if (u != NULL && HandleOrderVehClick(v, u, w)) return;

	const Order cmd = GetOrderCmdFromTile(v, tile);
	if (!cmd.IsValid()) return;

	if (DoCommandP(v->tile, v->index + (OrderGetSel(w) << 16), cmd.Pack(), NULL, CMD_INSERT_ORDER | CMD_MSG(STR_8833_CAN_T_INSERT_NEW_ORDER))) {
		if (WP(w, order_d).sel != -1) WP(w,order_d).sel++;
		ResetObjectToPlace();
	}
}

/**
 * Handle the click on the goto button.
 *
 * @param w current window
 * @param v current vehicle
 */
static void OrderClick_Goto(Window *w, const Vehicle *v, int i)
{
	w->InvalidateWidget(ORDER_WIDGET_GOTO);
	w->ToggleWidgetLoweredState(ORDER_WIDGET_GOTO);
	if (w->IsWidgetLowered(ORDER_WIDGET_GOTO)) {
		_place_clicked_vehicle = NULL;
		SetObjectToPlaceWnd(ANIMCURSOR_PICKSTATION, PAL_NONE, VHM_RECT, w);
	} else {
		ResetObjectToPlace();
	}
}

/**
 * Handle the click on the full load button.
 *
 * @param w current window
 * @param v current vehicle
 * @param load_type the way to load.
 */
static void OrderClick_FullLoad(Window *w, const Vehicle *v, int load_type)
{
	VehicleOrderID sel_ord = OrderGetSel(w);
	const Order *order = GetVehicleOrder(v, sel_ord);

	if (load_type >= 0 && order->GetLoadType() == load_type) return;

	if (load_type < 0) {
		switch (order->GetLoadType()) {
			case OLF_LOAD_IF_POSSIBLE: load_type = OLFB_FULL_LOAD;       break;
			case OLFB_FULL_LOAD:       load_type = OLF_FULL_LOAD_ANY;    break;
			case OLF_FULL_LOAD_ANY:    load_type = OLF_LOAD_IF_POSSIBLE; break;
			default: NOT_REACHED();
		}
	}
	DoCommandP(v->tile, v->index + (sel_ord << 16), MOF_LOAD | (load_type << 2), NULL, CMD_MODIFY_ORDER | CMD_MSG(STR_8835_CAN_T_MODIFY_THIS_ORDER));
}

/**
 * Handle the click on the service.
 *
 * @param w current window
 * @param v current vehicle
 */
static void OrderClick_Service(Window *w, const Vehicle *v, int i)
{
	DoCommandP(v->tile, v->index + (OrderGetSel(w) << 16), MOF_DEPOT_ACTION, NULL, CMD_MODIFY_ORDER | CMD_MSG(STR_8835_CAN_T_MODIFY_THIS_ORDER));
}

/**
 * Handle the click on the unload button.
 *
 * @param w current window
 * @param v current vehicle
 */
static void OrderClick_Unload(Window *w, const Vehicle *v, int i)
{
	VehicleOrderID sel_ord = OrderGetSel(w);
	const Order *order = GetVehicleOrder(v, sel_ord);

	DoCommandP(v->tile, v->index + (sel_ord << 16), MOF_UNLOAD | (order->GetUnloadType() ^ OUFB_UNLOAD) << 2, NULL, CMD_MODIFY_ORDER | CMD_MSG(STR_8835_CAN_T_MODIFY_THIS_ORDER));
}

/**
 * Handle the click on the nonstop button.
 *
 * @param w current window
 * @param v current vehicle
 * @param non_stop what non-stop type to use; -1 to use the 'next' one.
 */
static void OrderClick_Nonstop(Window *w, const Vehicle *v, int non_stop)
{
	VehicleOrderID sel_ord = OrderGetSel(w);
	const Order *order = GetVehicleOrder(v, sel_ord);

	if (order->GetNonStopType() == non_stop) return;

	/* Keypress if negative, so 'toggle' to the next */
	if (non_stop < 0) {
		non_stop = (order->GetNonStopType() + 1) % ONSF_END;
	}

	DoCommandP(v->tile, v->index + (sel_ord << 16), MOF_NON_STOP | non_stop << 2,  NULL, CMD_MODIFY_ORDER | CMD_MSG(STR_8835_CAN_T_MODIFY_THIS_ORDER));
}

/**
 * Handle the click on the transfer button.
 *
 * @param w current window
 * @param v current vehicle
 */
static void OrderClick_Transfer(Window *w, const Vehicle *v, int i)
{
	VehicleOrderID sel_ord = OrderGetSel(w);
	const Order *order = GetVehicleOrder(v, sel_ord);

	DoCommandP(v->tile, v->index + (sel_ord << 16), MOF_UNLOAD | (order->GetUnloadType() ^ OUFB_TRANSFER) << 2, NULL, CMD_MODIFY_ORDER | CMD_MSG(STR_8835_CAN_T_MODIFY_THIS_ORDER));
}

/**
 * Handle the click on the skip button.
 * If ctrl is pressed skip to selected order.
 *  Else skip to current order + 1
 *
 * @param w current window
 * @param v current vehicle
 */
static void OrderClick_Skip(Window *w, const Vehicle *v, int i)
{
	/* Don't skip when there's nothing to skip */
	if (_ctrl_pressed && v->cur_order_index == OrderGetSel(w)) return;

	DoCommandP(v->tile, v->index, _ctrl_pressed ? OrderGetSel(w) : ((v->cur_order_index + 1) % v->num_orders),
			NULL, CMD_SKIP_TO_ORDER | CMD_MSG(_ctrl_pressed ? STR_CAN_T_SKIP_TO_ORDER : STR_CAN_T_SKIP_ORDER));
}

/**
 * Handle the click on the unload button.
 *
 * @param w current window
 * @param v current vehicle
 */
static void OrderClick_Delete(Window *w, const Vehicle *v, int i)
{
	DoCommandP(v->tile, v->index, OrderGetSel(w), NULL, CMD_DELETE_ORDER | CMD_MSG(STR_8834_CAN_T_DELETE_THIS_ORDER));
}

/**
 * Handle the click on the refit button.
 * If ctrl is pressed cancel refitting.
 *  Else show the refit window.
 *
 * @param w current window
 * @param v current vehicle
 */
static void OrderClick_Refit(Window *w, const Vehicle *v, int i)
{
	if (_ctrl_pressed) {
		/* Cancel refitting */
		DoCommandP(v->tile, v->index, (WP(w, order_d).sel << 16) | (CT_NO_REFIT << 8) | CT_NO_REFIT, NULL, CMD_ORDER_REFIT);
	} else {
		ShowVehicleRefitWindow(v, WP(w, order_d).sel);
	}
}

typedef void OnButtonVehClick(Window *w, const Vehicle *v, int i);

/**
 * Keycode function mapping.
 *
 * @see _order_keycodes[]
 * @note Keep them allways in sync with _order_keycodes[]!
 */
static OnButtonVehClick* const _order_button_proc[] = {
	OrderClick_Skip,
	OrderClick_Delete,
	OrderClick_Nonstop,
	OrderClick_Goto,
	OrderClick_FullLoad,
	OrderClick_Unload,
	OrderClick_Transfer,
	OrderClick_Service,
};

static const uint16 _order_keycodes[] = {
	'D', //skip order
	'F', //delete order
	'G', //non-stop
	'H', //goto order
	'J', //full load
	'K'  //unload
};

static const StringID _order_non_stop_drowdown[] = {
	STR_ORDER_GO_TO,
	STR_ORDER_GO_NON_STOP_TO,
	STR_ORDER_GO_VIA,
	STR_ORDER_GO_NON_STOP_VIA,
	INVALID_STRING_ID
};

static const StringID _order_full_load_drowdown[] = {
	STR_ORDER_DROP_LOAD_IF_POSSIBLE,
	STR_ORDER_DROP_FULL_LOAD_ALL,
	STR_ORDER_DROP_FULL_LOAD_ANY,
	INVALID_STRING_ID
};

static void OrdersWndProc(Window *w, WindowEvent *e)
{
	const Vehicle *v = GetVehicle(w->window_number);

	switch (e->event) {
		case WE_CREATE:
			/* Ensure that the refit and unload buttons always remain at the same location.
			 * Only one of them can be active at any one time and takes turns on being disabled.
			 * To ensure that they stay at the same location, we also verify that they behave the same
			 * when resizing. */
			if (v->owner == _local_player) { // only the vehicle owner got these buttons
				assert(w->widget[ORDER_WIDGET_REFIT].left          == w->widget[ORDER_WIDGET_UNLOAD].left);
				assert(w->widget[ORDER_WIDGET_REFIT].right         == w->widget[ORDER_WIDGET_UNLOAD].right);
				assert(w->widget[ORDER_WIDGET_REFIT].top           == w->widget[ORDER_WIDGET_UNLOAD].top);
				assert(w->widget[ORDER_WIDGET_REFIT].bottom        == w->widget[ORDER_WIDGET_UNLOAD].bottom);
				assert(w->widget[ORDER_WIDGET_REFIT].display_flags == w->widget[ORDER_WIDGET_UNLOAD].display_flags);
			}

			if (_patches.timetabling) {
				w->widget[ORDER_WIDGET_CAPTION].right -= 61;
			} else {
				w->HideWidget(ORDER_WIDGET_TIMETABLE_VIEW);
			}

			break;

		case WE_PAINT:
			DrawOrdersWindow(w);
			break;

		case WE_CLICK:
			switch (e->we.click.widget) {
				case ORDER_WIDGET_ORDER_LIST: {
					ResetObjectToPlace();
					HideDropDownMenu(w);

					int sel = GetOrderFromOrderWndPt(w, e->we.click.pt.y, v);

					if (sel == INVALID_ORDER) {
						/* This was a click on an empty part of the orders window, so
						* deselect the currently selected order. */
						WP(w, order_d).sel = -1;
						SetWindowDirty(w);
						return;
					}

					if (_ctrl_pressed && sel < v->num_orders) {
						const Order *ord = GetVehicleOrder(v, sel);
						TileIndex xy;

						switch (ord->GetType()) {
							case OT_GOTO_STATION:  xy = GetStation(ord->GetDestination())->xy ; break;
							case OT_GOTO_DEPOT:    xy = (v->type == VEH_AIRCRAFT) ?  GetStation(ord->GetDestination())->xy : GetDepot(ord->GetDestination())->xy;    break;
							case OT_GOTO_WAYPOINT: xy = GetWaypoint(ord->GetDestination())->xy; break;
							default:               xy = 0; break;
						}

						if (xy != 0) ScrollMainWindowToTile(xy);
						return;
					} else {
						if (sel == WP(w, order_d).sel) {
							/* Deselect clicked order */
							WP(w, order_d).sel = -1;
						} else {
							/* Select clicked order */
							WP(w, order_d).sel = sel;

							if (v->owner == _local_player) {
								/* Activate drag and drop */
								SetObjectToPlaceWnd(SPR_CURSOR_MOUSE, PAL_NONE, VHM_DRAG, w);
							}
						}
					}

					SetWindowDirty(w);
				} break;

				case ORDER_WIDGET_SKIP:
					OrderClick_Skip(w, v, 0);
					break;

				case ORDER_WIDGET_DELETE:
					OrderClick_Delete(w, v, 0);
					break;

				case ORDER_WIDGET_NON_STOP: {
					const Order *o = GetVehicleOrder(v, OrderGetSel(w));
					ShowDropDownMenu(w, _order_non_stop_drowdown, o->GetNonStopType(), ORDER_WIDGET_NON_STOP, 0, o->IsType(OT_GOTO_STATION) ? 0 : (o->IsType(OT_GOTO_WAYPOINT) ? 3 : 12), 124);
				} break;

				case ORDER_WIDGET_GOTO:
					OrderClick_Goto(w, v, 0);
					break;

				case ORDER_WIDGET_FULL_LOAD:
					ShowDropDownMenu(w, _order_full_load_drowdown, GetVehicleOrder(v, OrderGetSel(w))->GetLoadType() << 2, ORDER_WIDGET_FULL_LOAD, 0, 0, 124);
					break;

				case ORDER_WIDGET_UNLOAD:
					OrderClick_Unload(w, v, 0);
					break;

				case ORDER_WIDGET_REFIT:
					OrderClick_Refit(w, v, 0);
					break;

				case ORDER_WIDGET_TRANSFER:
					OrderClick_Transfer(w, v, 0);
					break;

				case ORDER_WIDGET_SERVICE:
					OrderClick_Service(w, v, 0);
					break;

				case ORDER_WIDGET_TIMETABLE_VIEW:
					ShowTimetableWindow(v);
					break;

				case ORDER_WIDGET_SHARED_ORDER_LIST:
					ShowVehicleListWindow(v);
					break;
			}
			break;

		case WE_DROPDOWN_SELECT: // we have selected a dropdown item in the list
			switch (e->we.dropdown.button) {
				case ORDER_WIDGET_NON_STOP:
					OrderClick_Nonstop(w, v, e->we.dropdown.index);
					break;

				case ORDER_WIDGET_FULL_LOAD:
					OrderClick_FullLoad(w, v, e->we.dropdown.index == 0 ? 0 : e->we.dropdown.index + 1);
					break;
			}
			break;

		case WE_DRAGDROP:
			switch (e->we.click.widget) {
				case ORDER_WIDGET_ORDER_LIST: {
					int from_order = OrderGetSel(w);
					int to_order = GetOrderFromOrderWndPt(w, e->we.dragdrop.pt.y, v);

					if (!(from_order == to_order || from_order == INVALID_ORDER || from_order > v->num_orders || to_order == INVALID_ORDER || to_order > v->num_orders) &&
							DoCommandP(v->tile, v->index, from_order | (to_order << 16), NULL, CMD_MOVE_ORDER | CMD_MSG(STR_CAN_T_MOVE_THIS_ORDER))) {
						WP(w, order_d).sel = -1;
					}

				} break;

				case ORDER_WIDGET_DELETE:
					OrderClick_Delete(w, v, 0);
					break;
			}

			ResetObjectToPlace();
			break;

		case WE_KEYPRESS:
			if (v->owner != _local_player) break;

			for (uint i = 0; i < lengthof(_order_keycodes); i++) {
				if (e->we.keypress.keycode == _order_keycodes[i]) {
					e->we.keypress.cont = false;
					/* see if the button is disabled */
					if (!w->IsWidgetDisabled(i + ORDER_WIDGET_SKIP)) _order_button_proc[i](w, v, -1);
					break;
				}
			}
			break;

		case WE_RCLICK: {
			int s = OrderGetSel(w);

			if (e->we.click.widget != ORDER_WIDGET_FULL_LOAD) break;
			if (s == v->num_orders || !GetVehicleOrder(v, s)->IsType(OT_GOTO_DEPOT)) {
				GuiShowTooltips(STR_8857_MAKE_THE_HIGHLIGHTED_ORDER);
			} else {
				GuiShowTooltips(STR_SERVICE_HINT);
			}
		} break;

		case WE_PLACE_OBJ:
			OrdersPlaceObj(GetVehicle(w->window_number), e->we.place.tile, w);
			break;

		case WE_ABORT_PLACE_OBJ:
			w->RaiseWidget(ORDER_WIDGET_GOTO);
			w->InvalidateWidget(ORDER_WIDGET_GOTO);
			break;

		/* check if a vehicle in a depot was clicked.. */
		case WE_MOUSELOOP:
			v = _place_clicked_vehicle;
			/*
			* Check if we clicked on a vehicle
			* and if the GOTO button of this window is pressed
			* This is because of all open order windows WE_MOUSELOOP is called
			* and if you have 3 windows open, and this check is not done
			* the order is copied to the last open window instead of the
			* one where GOTO is enabled
			*/
			if (v != NULL && w->IsWidgetLowered(ORDER_WIDGET_GOTO)) {
				_place_clicked_vehicle = NULL;
				HandleOrderVehClick(GetVehicle(w->window_number), v, w);
			}
			break;

		case WE_RESIZE:
			/* Update the scroll + matrix */
			w->vscroll.cap = (w->widget[ORDER_WIDGET_ORDER_LIST].bottom - w->widget[ORDER_WIDGET_ORDER_LIST].top) / 10;
			break;

		case WE_TIMEOUT: // handle button unclick ourselves...
			/* unclick all buttons except for the 'goto' button (ORDER_WIDGET_GOTO), which is 'persistent' */
			for (uint i = 0; i < w->widget_count; i++) {
				if (w->IsWidgetLowered(i) && i != ORDER_WIDGET_GOTO) {
					w->RaiseWidget(i);
					w->InvalidateWidget(i);
				}
			}
			break;
	}
}

/**
 * Widget definition for player train orders
 */
static const Widget _orders_train_widgets[] = {
	{   WWT_CLOSEBOX,   RESIZE_NONE,    14,     0,    10,     0,    13, STR_00C5,                STR_018B_CLOSE_WINDOW},               // ORDER_WIDGET_CLOSEBOX
	{    WWT_CAPTION,   RESIZE_RIGHT,   14,    11,   385,     0,    13, STR_8829_ORDERS,         STR_018C_WINDOW_TITLE_DRAG_THIS},     // ORDER_WIDGET_CAPTION
	{ WWT_PUSHTXTBTN,   RESIZE_LR,      14,   325,   385,     0,    13, STR_TIMETABLE_VIEW,      STR_TIMETABLE_VIEW_TOOLTIP},          // ORDER_WIDGET_TIMETABLE_VIEW

	{      WWT_PANEL,   RESIZE_RB,      14,     0,   373,    14,    75, 0x0,                     STR_8852_ORDERS_LIST_CLICK_ON_ORDER}, // ORDER_WIDGET_ORDER_LIST

	{  WWT_SCROLLBAR,   RESIZE_LRB,     14,   374,   385,    14,    75, 0x0,                     STR_0190_SCROLL_BAR_SCROLLS_LIST},    // ORDER_WIDGET_SCROLLBAR

	{ WWT_PUSHTXTBTN,   RESIZE_TB,      14,     0,   123,    88,    99, STR_8823_SKIP,           STR_8853_SKIP_THE_CURRENT_ORDER},     // ORDER_WIDGET_SKIP
	{ WWT_PUSHTXTBTN,   RESIZE_TB,      14,   124,   247,    88,    99, STR_8824_DELETE,         STR_8854_DELETE_THE_HIGHLIGHTED},     // ORDER_WIDGET_DELETE
	{   WWT_DROPDOWN,   RESIZE_TB,      14,     0,    92,    76,    87, STR_8825_NON_STOP,       STR_8855_MAKE_THE_HIGHLIGHTED_ORDER}, // ORDER_WIDGET_NON_STOP
	{    WWT_TEXTBTN,   RESIZE_TB,      14,   248,   371,    88,    99, STR_8826_GO_TO,          STR_8856_INSERT_A_NEW_ORDER_BEFORE},  // ORDER_WIDGET_GOTO
	{   WWT_DROPDOWN,   RESIZE_TB,      14,    93,   185,    76,    87, STR_8827_FULL_LOAD,      STR_8857_MAKE_THE_HIGHLIGHTED_ORDER}, // ORDER_WIDGET_FULL_LOAD
	{ WWT_PUSHTXTBTN,   RESIZE_TB,      14,   186,   278,    76,    87, STR_8828_UNLOAD,         STR_8858_MAKE_THE_HIGHLIGHTED_ORDER}, // ORDER_WIDGET_UNLOAD
	{ WWT_PUSHTXTBTN,   RESIZE_TB,      14,   186,   278,    76,    87, STR_REFIT,               STR_REFIT_TIP},                       // ORDER_WIDGET_REFIT
	{ WWT_PUSHTXTBTN,   RESIZE_TB,      14,   279,   371,    76,    87, STR_TRANSFER,            STR_MAKE_THE_HIGHLIGHTED_ORDER},      // ORDER_WIDGET_TRANSFER
	{ WWT_PUSHTXTBTN,   RESIZE_TB,      14,   279,   371,    76,    87, STR_SERVICE,             STR_MAKE_THE_HIGHLIGHTED_ORDER},      // ORDER_WIDGET_SERVICE

	{      WWT_PANEL,   RESIZE_RTB,     14,   372,   373,    76,    99, 0x0,                     STR_NULL},                            // ORDER_WIDGET_RESIZE_BAR
	{ WWT_PUSHIMGBTN,   RESIZE_LRTB,    14,   372,   385,    76,    87, SPR_SHARED_ORDERS_ICON,  STR_VEH_WITH_SHARED_ORDERS_LIST_TIP}, // ORDER_WIDGET_SHARED_ORDER_LIST

	{  WWT_RESIZEBOX,   RESIZE_LRTB,    14,   374,   385,    88,    99, 0x0,                     STR_RESIZE_BUTTON},                   // ORDER_WIDGET_RESIZE
	{   WIDGETS_END},
};

static const WindowDesc _orders_train_desc = {
	WDP_AUTO, WDP_AUTO, 386, 100, 386, 100,
	WC_VEHICLE_ORDERS,WC_VEHICLE_VIEW,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_RESIZABLE,
	_orders_train_widgets,
	OrdersWndProc
};

/**
 * Widget definition for player orders (!train)
 */
static const Widget _orders_widgets[] = {
	{   WWT_CLOSEBOX,   RESIZE_NONE,    14,     0,    10,     0,    13, STR_00C5,                STR_018B_CLOSE_WINDOW},               // ORDER_WIDGET_CLOSEBOX
	{    WWT_CAPTION,   RESIZE_RIGHT,   14,    11,   385,     0,    13, STR_8829_ORDERS,         STR_018C_WINDOW_TITLE_DRAG_THIS},     // ORDER_WIDGET_CAPTION
	{ WWT_PUSHTXTBTN,   RESIZE_LR,      14,   325,   385,     0,    13, STR_TIMETABLE_VIEW,      STR_TIMETABLE_VIEW_TOOLTIP},          // ORDER_WIDGET_TIMETABLE_VIEW

	{      WWT_PANEL,   RESIZE_RB,      14,     0,   373,    14,    75, 0x0,                     STR_8852_ORDERS_LIST_CLICK_ON_ORDER}, // ORDER_WIDGET_ORDER_LIST

	{  WWT_SCROLLBAR,   RESIZE_LRB,     14,   374,   385,    14,    75, 0x0,                     STR_0190_SCROLL_BAR_SCROLLS_LIST},    // ORDER_WIDGET_SCROLLBAR

	{ WWT_PUSHTXTBTN,   RESIZE_TB,      14,     0,   123,    88,    99, STR_8823_SKIP,           STR_8853_SKIP_THE_CURRENT_ORDER},     // ORDER_WIDGET_SKIP
	{ WWT_PUSHTXTBTN,   RESIZE_TB,      14,   124,   247,    88,    99, STR_8824_DELETE,         STR_8854_DELETE_THE_HIGHLIGHTED},     // ORDER_WIDGET_DELETE
	{      WWT_EMPTY,   RESIZE_TB,      14,     0,     0,    76,    87, 0x0,                     0x0},                                 // ORDER_WIDGET_NON_STOP
	{    WWT_TEXTBTN,   RESIZE_TB,      14,   248,   371,    88,    99, STR_8826_GO_TO,          STR_8856_INSERT_A_NEW_ORDER_BEFORE},  // ORDER_WIDGET_GOTO
	{   WWT_DROPDOWN,   RESIZE_TB,      14,     0,   123,    76,    87, STR_8827_FULL_LOAD,      STR_8857_MAKE_THE_HIGHLIGHTED_ORDER}, // ORDER_WIDGET_FULL_LOAD
	{ WWT_PUSHTXTBTN,   RESIZE_TB,      14,   124,   247,    76,    87, STR_8828_UNLOAD,         STR_8858_MAKE_THE_HIGHLIGHTED_ORDER}, // ORDER_WIDGET_UNLOAD
	{ WWT_PUSHTXTBTN,   RESIZE_TB,      14,   124,   247,    76,    87, STR_REFIT,               STR_REFIT_TIP},                       // ORDER_WIDGET_REFIT
	{ WWT_PUSHTXTBTN,   RESIZE_TB,      14,   248,   372,    76,    87, STR_TRANSFER,            STR_MAKE_THE_HIGHLIGHTED_ORDER},      // ORDER_WIDGET_TRANSFER
	{ WWT_PUSHTXTBTN,   RESIZE_TB,      14,   248,   372,    76,    87, STR_SERVICE,             STR_NULL},                            // ORDER_WIDGET_SERVICE

	{      WWT_PANEL,   RESIZE_RTB,     14,   372,   373,    76,    99, 0x0,                     STR_NULL},                            // ORDER_WIDGET_RESIZE_BAR
	{ WWT_PUSHIMGBTN,   RESIZE_LRTB,    14,   372,   385,    76,    87, SPR_SHARED_ORDERS_ICON,  STR_VEH_WITH_SHARED_ORDERS_LIST_TIP}, // ORDER_WIDGET_SHARED_ORDER_LIST

	{  WWT_RESIZEBOX,   RESIZE_LRTB,    14,   374,   385,    88,    99, 0x0,                     STR_RESIZE_BUTTON},                   // ORDER_WIDGET_RESIZE
	{   WIDGETS_END},
};

static const WindowDesc _orders_desc = {
	WDP_AUTO, WDP_AUTO, 386, 100, 386, 100,
	WC_VEHICLE_ORDERS,WC_VEHICLE_VIEW,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_RESIZABLE,
	_orders_widgets,
	OrdersWndProc
};

/**
 * Widget definition for competitor orders
 */
static const Widget _other_orders_widgets[] = {
	{   WWT_CLOSEBOX,   RESIZE_NONE,    14,     0,    10,     0,    13, STR_00C5,           STR_018B_CLOSE_WINDOW},               // ORDER_WIDGET_CLOSEBOX
	{    WWT_CAPTION,   RESIZE_RIGHT,   14,    11,   385,     0,    13, STR_8829_ORDERS,    STR_018C_WINDOW_TITLE_DRAG_THIS},     // ORDER_WIDGET_CAPTION
	{ WWT_PUSHTXTBTN,   RESIZE_LR,      14,   325,   385,     0,    13, STR_TIMETABLE_VIEW, STR_TIMETABLE_VIEW_TOOLTIP},          // ORDER_WIDGET_TIMETABLE_VIEW

	{      WWT_PANEL,   RESIZE_RB,      14,     0,   373,    14,    75, 0x0,                STR_8852_ORDERS_LIST_CLICK_ON_ORDER}, // ORDER_WIDGET_ORDER_LIST

	{  WWT_SCROLLBAR,   RESIZE_LRB,     14,   374,   385,    14,    75, 0x0,                STR_0190_SCROLL_BAR_SCROLLS_LIST},    // ORDER_WIDGET_SCROLLBAR

	{      WWT_EMPTY,   RESIZE_NONE,    14,     0,     0,    76,    87, 0x0,                STR_NULL},                            // ORDER_WIDGET_SKIP
	{      WWT_EMPTY,   RESIZE_NONE,    14,     0,     0,    76,    87, 0x0,                STR_NULL},                            // ORDER_WIDGET_DELETE
	{      WWT_EMPTY,   RESIZE_NONE,    14,     0,     0,    76,    87, 0x0,                STR_NULL},                            // ORDER_WIDGET_NON_STOP
	{      WWT_EMPTY,   RESIZE_NONE,    14,     0,     0,    76,    87, 0x0,                STR_NULL},                            // ORDER_WIDGET_GOTO
	{      WWT_EMPTY,   RESIZE_NONE,    14,     0,     0,    76,    87, 0x0,                STR_NULL},                            // ORDER_WIDGET_FULL_LOAD
	{      WWT_EMPTY,   RESIZE_NONE,    14,     0,     0,    76,    87, 0x0,                STR_NULL},                            // ORDER_WIDGET_UNLOAD
	{      WWT_EMPTY,   RESIZE_NONE,    14,     0,     0,    76,    87, 0x0,                STR_NULL},                            // ORDER_WIDGET_REFIT
	{      WWT_EMPTY,   RESIZE_NONE,    14,     0,     0,    76,    87, 0x0,                STR_NULL},                            // ORDER_WIDGET_TRANSFER
	{      WWT_EMPTY,   RESIZE_NONE,    14,     0,     0,    76,    87, 0x0,                STR_NULL},                            // ORDER_WIDGET_SERVICE

	{      WWT_PANEL,   RESIZE_RTB,     14,     0,   373,    76,    87, 0x0,                STR_NULL},                            // ORDER_WIDGET_RESIZE_BAR
	{      WWT_EMPTY,   RESIZE_TB,      14,     0,     0,    76,    87, 0x0,                STR_NULL},                            // ORDER_WIDGET_SHARED_ORDER_LIST

	{  WWT_RESIZEBOX,   RESIZE_LRTB,    14,   374,   385,    88,    99, 0x0,                STR_RESIZE_BUTTON},              // ORDER_WIDGET_RESIZE
	{   WIDGETS_END},
};

static const WindowDesc _other_orders_desc = {
	WDP_AUTO, WDP_AUTO, 386, 88, 386, 88,
	WC_VEHICLE_ORDERS,WC_VEHICLE_VIEW,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_UNCLICK_BUTTONS | WDF_RESIZABLE,
	_other_orders_widgets,
	OrdersWndProc
};

void ShowOrdersWindow(const Vehicle *v)
{
	Window *w;
	VehicleID veh = v->index;

	DeleteWindowById(WC_VEHICLE_ORDERS, veh);
	DeleteWindowById(WC_VEHICLE_DETAILS, veh);

	if (v->owner != _local_player) {
		w = AllocateWindowDescFront(&_other_orders_desc, veh);
	} else {
		w = AllocateWindowDescFront((v->type == VEH_TRAIN) ? &_orders_train_desc : &_orders_desc, veh);
	}

	if (w != NULL) {
		w->caption_color = v->owner;
		w->vscroll.cap = 6;
		w->resize.step_height = 10;
		WP(w, order_d).sel = -1;
	}
}
