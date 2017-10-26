#include "tool_route_track_interactive.hpp"
#include <iostream>
#include "core_board.hpp"
#include "imp_interface.hpp"
#include "board/board_rules.hpp"
#include "util.hpp"
#include "router/router/pns_router.h"
#include "router/pns_horizon_iface.h"
#include "canvas/canvas.hpp"
#include "router/pns_solid.h"
#include "board_layers.hpp"

namespace horizon {

	class ToolWrapper {
		public:
		ToolWrapper(ToolRouteTrackInteractive *t):tool(t) {}

		void updateStartItem(const ToolArgs &args);
		void updateEndItem(const ToolArgs &args);
		PNS::ITEM* pickSingleItem( const VECTOR2I& aWhere, int aNet=-1, int aLayer=-1 );
		const VECTOR2I snapToItem( bool aEnabled, PNS::ITEM* aItem, VECTOR2I aP);
		bool prepareInteractive();
		int getStartLayer( const PNS::ITEM* aItem );
		PNS::ITEM *m_startItem = nullptr;
		PNS::ITEM *m_endItem = nullptr;
		VECTOR2I m_startSnapPoint;
		VECTOR2I m_endSnapPoint;
		int work_layer=0;


		ToolRouteTrackInteractive *tool = nullptr;
	};

	ToolRouteTrackInteractive::ToolRouteTrackInteractive(Core *c, ToolID tid):ToolBase(c, tid) {
		name = "Route Track interactive";
	}

	bool ToolRouteTrackInteractive::can_begin() {
		return core.b;
	}

	ToolRouteTrackInteractive::~ToolRouteTrackInteractive() {
		delete router;
		delete iface;
		delete wrapper;
	}

	ToolResponse ToolRouteTrackInteractive::begin(const ToolArgs &args) {
		std::cout << "tool route track\n";
		core.r->selection.clear();
		canvas = imp->get_canvas();
		canvas->set_cursor_external(true);
		imp->set_no_update(true);

		wrapper = new ToolWrapper(this);

		board = core.b->get_board();
		rules = dynamic_cast<BoardRules*>(core.r->get_rules());

		iface = new PNS::PNS_HORIZON_IFACE;
		iface->SetBoard( board );
		iface->create_debug_decorator(imp->get_canvas());
		iface->SetCanvas( imp->get_canvas() );
		iface->SetRules(rules);
		iface->SetViaPadstackProvider(core.b->get_via_padstack_provider());
		//m_iface->SetHostFrame( m_frame );

		router = new PNS::ROUTER;
		router->SetInterface(iface);
		router->ClearWorld();
		router->SyncWorld();

		PNS::ROUTING_SETTINGS settings;
		settings.SetShoveVias(false);

		PNS::SIZES_SETTINGS sizes_settings;

		router->LoadSettings(settings);
		router->UpdateSizes(sizes_settings);

		imp->canvas_update();
		update_tip();
		return ToolResponse();
	}

	PNS::ITEM* ToolWrapper::pickSingleItem( const VECTOR2I& aWhere, int aNet, int aLayer ) {
		int tl = PNS::PNS_HORIZON_IFACE::layer_to_router(tool->imp->get_work_layer());


		if( aLayer > 0 )
			tl = aLayer;

		PNS::ITEM* prioritized[4] = {0};

		PNS::ITEM_SET candidates = tool->router->QueryHoverItems( aWhere );

		for( PNS::ITEM* item : candidates.Items() )
		{
			//if( !IsCopperLayer( item->Layers().Start() ) )
			//	continue;

			// fixme: this causes flicker with live loop removal...
			//if( item->Parent() && !item->Parent()->ViewIsVisible() )
			//    continue;
			auto la = PNS::PNS_HORIZON_IFACE::layer_from_router(item->Layers().Start());
			if(!tool->canvas->layer_is_visible(la))
				continue;

			if( aNet < 0 || item->Net() == aNet )
			{
				if( item->OfKind( PNS::ITEM::VIA_T | PNS::ITEM::SOLID_T ) )
				{
					if( !prioritized[2] )
						prioritized[2] = item;
					if( item->Layers().Overlaps( tl ) )
						prioritized[0] = item;
				}
				else
				{
					if( !prioritized[3] )
						prioritized[3] = item;
					if( item->Layers().Overlaps( tl ) )
						prioritized[1] = item;
				}
			}
		}

		PNS::ITEM* rv = NULL;
		//PCB_EDIT_FRAME* frame = getEditFrame<PCB_EDIT_FRAME>();
		//DISPLAY_OPTIONS* displ_opts = (DISPLAY_OPTIONS*)frame->GetDisplayOptions();

		for( int i = 0; i < 4; i++ )
		{
			PNS::ITEM* item = prioritized[i];

			if( tool->canvas->selection_filter.work_layer_only )
				if( item && !item->Layers().Overlaps( tl ) )
					item = NULL;

			if( item )
			{
				rv = item;
				break;
			}
		}

		if( rv && aLayer >= 0 && !rv->Layers().Overlaps( aLayer ) )
			rv = NULL;

		if( rv )
		{
			wxLogTrace( "PNS", "%s, layer : %d, tl: %d", rv->KindStr().c_str(), rv->Layers().Start(), tl );
		}

		return rv;
	}

	static VECTOR2I AlignToSegment( const VECTOR2I& aPoint, const VECTOR2I &snapped, const SEG& aSeg ) {
    OPT_VECTOR2I pts[6];

    VECTOR2I nearest = snapped;

    pts[0] = aSeg.A;
    pts[1] = aSeg.B;
    pts[2] = aSeg.IntersectLines( SEG( nearest, nearest + VECTOR2I( 1, 0 ) ) );
    pts[3] = aSeg.IntersectLines( SEG( nearest, nearest + VECTOR2I( 0, 1 ) ) );

    int min_d = std::numeric_limits<int>::max();

    for( int i = 0; i < 4; i++ )
    {
        if( pts[i] && aSeg.Contains( *pts[i] ) )
        {
            int d = (*pts[i] - aPoint).EuclideanNorm();

            if( d < min_d )
            {
                min_d = d;
                nearest = *pts[i];
            }
        }
    }

    return nearest;
}

	const VECTOR2I ToolWrapper::snapToItem( bool aEnabled, PNS::ITEM* aItem, VECTOR2I aP) {
		VECTOR2I anchor;

		if( !aItem || !aEnabled )
		{
			auto snapped = tool->canvas->snap_to_grid(Coordi(aP.x, aP.y));
			return VECTOR2I(snapped.x, snapped.y);
		}

		switch( aItem->Kind() )
		{
		case PNS::ITEM::SOLID_T:
			anchor = static_cast<PNS::SOLID*>( aItem )->Pos();
			break;

		case PNS::ITEM::VIA_T:
			anchor = static_cast<PNS::VIA*>( aItem )->Pos();
			break;

		case PNS::ITEM::SEGMENT_T:
		{
			PNS::SEGMENT* seg = static_cast<PNS::SEGMENT*>( aItem );
			const SEG& s = seg->Seg();
			int w = seg->Width();


			if( ( aP - s.A ).EuclideanNorm() < w / 2 )
				anchor = s.A;
			else if( ( aP - s.B ).EuclideanNorm() < w / 2 )
				anchor = s.B;
			else {
				auto snapped = tool->canvas->snap_to_grid(Coordi(aP.x, aP.y));
				anchor = AlignToSegment( aP, VECTOR2I(snapped.x, snapped.y), s );
			}break;
		}

		default:
			break;
		}

		return anchor;
	}


	void ToolWrapper::updateStartItem(const ToolArgs &args) {
		int tl = PNS::PNS_HORIZON_IFACE::layer_to_router(args.work_layer);
		work_layer = tl;
		VECTOR2I cp(args.coords.x, args.coords.y);
		VECTOR2I p;

		bool snapEnabled = true;

		/*if( aEvent.IsMotion() || aEvent.IsClick() )
		{
			snapEnabled = !aEvent.Modifier( MD_SHIFT );
			p = aEvent.Position();
		}
		else
		{
			p = cp;
		}*/
		p = cp;

		m_startItem = pickSingleItem( p );

		if( !snapEnabled && m_startItem && !m_startItem->Layers().Overlaps( tl ) )
			m_startItem = nullptr;

		m_startSnapPoint = snapToItem( snapEnabled, m_startItem, p );
		tool->canvas->set_cursor_pos(Coordi(m_startSnapPoint.x, m_startSnapPoint.y));
	}

	int ToolWrapper::getStartLayer( const PNS::ITEM* aItem )
	{
		int wl = tool->imp->get_canvas()->property_work_layer();
		int tl = PNS::PNS_HORIZON_IFACE::layer_to_router(wl);

		if( m_startItem )
		{
			const LAYER_RANGE& ls = m_startItem->Layers();

			if( ls.Overlaps( tl ) )
				return tl;
			else
				return ls.Start();
		}

		return tl;
	}

	const PNS::PNS_HORIZON_PARENT_ITEM *inheritTrackWidth( PNS::ITEM* aItem )	{
		using namespace PNS;
		VECTOR2I p;

		assert( aItem->Owner() != NULL );

		switch( aItem->Kind() )
		{
		case ITEM::VIA_T:
			p = static_cast<PNS::VIA*>( aItem )->Pos();
			break;

		case ITEM::SOLID_T:
			p = static_cast<SOLID*>( aItem )->Pos();
			break;

		case ITEM::SEGMENT_T:
			return static_cast<SEGMENT*>( aItem )->Parent();

		default:
			return 0;
		}

		JOINT* jt = static_cast<NODE*>( aItem->Owner() )->FindJoint( p, aItem );

		assert( jt != NULL );

		int mval = INT_MAX;


		ITEM_SET linkedSegs = jt->Links();
		linkedSegs.ExcludeItem( aItem ).FilterKinds( ITEM::SEGMENT_T );

		const PNS::PNS_HORIZON_PARENT_ITEM * parent = 0;

		for( ITEM* item : linkedSegs.Items() )
		{
			int w = static_cast<SEGMENT*>( item )->Width();
			if(w < mval) {
				parent = item->Parent();
				mval = w;
			}
			mval = std::min( w, mval );
		}

		return ( mval == INT_MAX ? 0 : parent);
	}


	bool ToolWrapper::prepareInteractive() {
		int routingLayer = getStartLayer( m_startItem );

		if( !IsCopperLayer( routingLayer ) )
		{
			tool->imp->tool_bar_flash("Tracks on Copper layers only");
			return false;
		}

		tool->imp->set_work_layer(PNS::PNS_HORIZON_IFACE::layer_from_router(routingLayer));

		PNS::SIZES_SETTINGS sizes( tool->router->Sizes() );

		int64_t track_width = 0;

		if(m_startItem) {
			auto parent = inheritTrackWidth(m_startItem);
			if(parent && parent->track) {
				auto track = parent->track;
				sizes.SetWidthFromRules(track->width_from_rules);
				track_width = track->width;
			}
		}

		if(!track_width) {
			if(m_startItem) {
				auto netcode = m_startItem->Net();
				auto net = tool->iface->get_net_for_code(netcode);
				if(net) {
					track_width = tool->rules->get_default_track_width(net, PNS::PNS_HORIZON_IFACE::layer_from_router(routingLayer));
					sizes.SetWidthFromRules(true);
				}
			}
		}

		sizes.SetTrackWidth(track_width);

		if(m_startItem) {
			auto netcode = m_startItem->Net();
			auto net = tool->iface->get_net_for_code(netcode);
			auto vps = tool->rules->get_via_parameter_set(net);
			if(vps.count(horizon::ParameterID::VIA_DIAMETER)) {
				sizes.SetViaDiameter(vps.at(horizon::ParameterID::VIA_DIAMETER));
			}
			if(vps.count(horizon::ParameterID::HOLE_DIAMETER)) {
				sizes.SetViaDrill(vps.at(horizon::ParameterID::HOLE_DIAMETER));
			}
		}

		/*sizes.Init( m_board, m_startItem );
		sizes.AddLayerPair( m_frame->GetScreen()->m_Route_Layer_TOP,
							m_frame->GetScreen()->m_Route_Layer_BOTTOM );
		*/
		tool->router->UpdateSizes( sizes );
		PNS::ROUTING_SETTINGS settings(tool->router->Settings());
		settings.SetMode(tool->shove?PNS::RM_Shove:PNS::RM_Walkaround);
		tool->router->LoadSettings(settings);

		if( !tool->router->StartRouting( m_startSnapPoint, m_startItem, routingLayer ) )
		{
			std::cout << "error " << tool->router->FailureReason() << std::endl;
			return false;
		}

		m_endItem = NULL;
		m_endSnapPoint = m_startSnapPoint;

		return true;
	}

	void ToolWrapper::updateEndItem( const ToolArgs & args) {
		VECTOR2I p(args.coords.x, args.coords.y);
		int layer;

		bool snapEnabled = true;
		/*
		if( m_router->GetCurrentNets().empty() || m_router->GetCurrentNets().front() < 0 )
		{
			m_endSnapPoint = snapToItem( snapEnabled, nullptr, p );
			m_ctls->ForceCursorPosition( true, m_endSnapPoint );
			m_endItem = nullptr;

			return;
		}*/

		if( tool->router->IsPlacingVia() )
			layer = -1;
		else
			layer = tool->router->GetCurrentLayer();

		PNS::ITEM* endItem = nullptr;

		std::vector<int> nets = tool->router->GetCurrentNets();

		for( int net : nets )
		{
			endItem = pickSingleItem( p, net, layer );

			if( endItem )
				break;
		}

		VECTOR2I cursorPos = snapToItem( snapEnabled, endItem, p );
		tool->canvas->set_cursor_pos({cursorPos.x, cursorPos.y});
		m_endItem = endItem;
		m_endSnapPoint = cursorPos;

		if( m_endItem )
		{
			wxLogTrace( "PNS", "%s, layer : %d", m_endItem->KindStr().c_str(), m_endItem->Layers().Start() );
		}
	}

	ToolResponse ToolRouteTrackInteractive::update(const ToolArgs &args) {
		if(state == State::START) {
			if(args.type == ToolEventType::MOVE) {
				wrapper->updateStartItem(args);
			}
			else if(args.type == ToolEventType::KEY) {
				if(args.key == GDK_KEY_s) {
					shove ^= true;
				}
			}
			else if(args.type == ToolEventType::CLICK) {
				if(args.button == 1) {
					state = State::ROUTING;
					if(!wrapper->prepareInteractive()) {
						return ToolResponse::end();
					}
				}
				else if(args.button == 3) {
					core.r->commit();
					return ToolResponse::end();
				}
			}
		}
		else if(state==State::ROUTING) {
			if(args.type == ToolEventType::MOVE) {
				wrapper->updateEndItem( args );
				router->Move(wrapper->m_endSnapPoint, wrapper->m_endItem);
			}
			else if(args.type == ToolEventType::CLICK) {
				if(args.button == 1) {
					  wrapper->updateEndItem( args );
					  bool needLayerSwitch = router->IsPlacingVia();

					  if(router->FixRoute( wrapper->m_endSnapPoint, wrapper->m_endItem ) ) {
						  router->StopRouting();
						  imp->canvas_update();
						  state = State::START;
						  update_tip();
						  return ToolResponse();
					  }
					  imp->canvas_update();

					  //if( needLayerSwitch )
					  //	  switchLayerOnViaPlacement();

					  // Synchronize the indicated layer
					  imp->set_work_layer(PNS::PNS_HORIZON_IFACE::layer_from_router(router->GetCurrentLayer()) );
					  wrapper->updateEndItem( args );
					  router->Move( wrapper->m_endSnapPoint, wrapper->m_endItem );
					  wrapper->m_startItem = NULL;
				}
				else if(args.button == 3) {
					core.r->commit();
					return ToolResponse::end();
				}
			}
			else if(args.type == ToolEventType::LAYER_CHANGE) {
				if(BoardLayers::is_copper(args.work_layer)) {
					router->SwitchLayer(PNS::PNS_HORIZON_IFACE::layer_to_router(args.work_layer));
					wrapper->updateEndItem( args );
					router->Move( wrapper->m_endSnapPoint, wrapper->m_endItem );
				}
			}
			else if(args.type == ToolEventType::KEY) {
				if(args.key == GDK_KEY_slash) {
					 router->FlipPosture();
					 wrapper->updateEndItem( args );
					 router->Move( wrapper->m_endSnapPoint, wrapper->m_endItem );
				}
				else if(args.key == GDK_KEY_v) {
					 router->ToggleViaPlacement();
					 wrapper->updateEndItem( args );
					 router->Move( wrapper->m_endSnapPoint, wrapper->m_endItem );
				}
			}
		}
		if(args.type == ToolEventType::KEY) {
			if(args.key == GDK_KEY_w) {
				PNS::SIZES_SETTINGS sz (router->Sizes());
				auto r = imp->dialogs.ask_datum("Track width", sz.TrackWidth());
				if(r.first) {
					sz.SetTrackWidth(r.second);
					sz.SetWidthFromRules(false);
					router->UpdateSizes(sz);
					router->Move( wrapper->m_endSnapPoint, wrapper->m_endItem );
				}
			}
			if(args.key == GDK_KEY_W) {
				auto nets = router->GetCurrentNets();
				Net *net = nullptr;
				for(auto x: nets) {
					net = iface->get_net_for_code(x);
				}
				if(net) {
					PNS::SIZES_SETTINGS sz (router->Sizes());
					sz.SetTrackWidth(rules->get_default_track_width(net, PNS::PNS_HORIZON_IFACE::layer_from_router(router->GetCurrentLayer())));
					sz.SetWidthFromRules(true);
					router->UpdateSizes(sz);
					router->Move( wrapper->m_endSnapPoint, wrapper->m_endItem );
				}
			}

			else if(args.key == GDK_KEY_Escape) {
				core.b->revert();
				core.b->get_board()->obstacles.clear();
				core.b->get_board()->track_path.clear();
				return ToolResponse::end();
			}
		}
		update_tip();
		return ToolResponse();
	}


	void ToolRouteTrackInteractive::update_tip() {
		std::stringstream ss;
		if(state==State::ROUTING) {
			ss << "<b>LMB:</b>place junction/connect <b>RMB:</b>finish and delete last segment <b>/:</b>track posture <b>v:</b>toggle via <i>";
			auto nets = router->GetCurrentNets();
			Net *net = nullptr;
			for(auto x: nets) {
				net = iface->get_net_for_code(x);
			}
			if(net) {
				if(net->name.size()) {
					ss << "routing net \"" << net->name << "\"";
				}
				else {
					ss << "routing unnamed net";
				}
			}
			else {
				ss << "routing no net";
			}

			PNS::SIZES_SETTINGS sz (router->Sizes());
			ss << "  track width " << dim_to_string(sz.TrackWidth());
			if(sz.WidthFromRules()) {
				ss << " (default)";
			}
			ss<<"</i>";
		}
		else {
			ss << "<b>LMB:</b>select starting junction/pad <b>RMB:</b>cancel <b>s:</b>shove/walkaround ";
			ss << "<i>";
			ss << "Mode: ";
			if(shove)
				ss << "shove";
			else
				ss << "walkaround";
			if(wrapper->m_startItem) {
				auto nc = wrapper->m_startItem->Net();
				auto net = iface->get_net_for_code(nc);

				if(net)
					ss << " Current Net: " << net->name;;


			}
			ss << "</i>";
		}
		imp->tool_bar_set_tip(ss.str());
	}
}
