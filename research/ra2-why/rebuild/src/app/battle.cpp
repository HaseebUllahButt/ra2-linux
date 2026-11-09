#include "battle_formation.h"
#include "battle.h"
#include "cameo_click.h"
#include "battle_status.h"
#include "sidebar_choices.h"
#include "camera_scroll.h"
#include "house_color.h"
#include "battle_hud.h"
#include "battle_queue.h"
#include "battle_score.h"
#include "battle_help.h"
#include "battle_cursor.h"
#include "battle_orders.h"
#include "battle_scene.h"
#include "battle_effects.h"
#include "battle_audio.h"
#include "scene.h"
#include "terrain_inputs.h"
#include "../session/world/setup.h"
#include "battle_setup.h"
#include "../sim/prototype_ai.h"
#include "../sim/prototype_economy.h"
#include "../sim/prototype_movement.h"
#include "../sim/prototype_production.h"
#include "../sim/prototype_tech.h"
#include "../sim/prototype_victory.h"
#include "../render/battle_radar.h"
#include "../sim/damage_runtime.h"
#include "../loop/game_loop.h"
#include "../loop/profile.h"
#include "../ui/score_screen.h"
#include <SDL.h>
#include <algorithm>
#include <future>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace ra2::app {
namespace {
namespace w=session::world;
namespace p=sim::production;
namespace proto=p::prototype;
using sim::skirmish::Order;
using sim::skirmish::OrderKind;

std::uint32_t frame_delay_ticks(const BattleOptions& args) {
    return std::uint32_t(std::clamp(args.setup.game_speed.value_or(2),0,6));
}

bool live(const w::Entity& e) {
    return e.lifetime==w::Lifetime::Placed && e.resolved_hp.value_or(0)>0 && e.cell && e.position;
}
const w::Entity* find(const w::Snapshot& s,std::uint32_t id) {
    const auto it=std::find_if(s.entities.begin(),s.entities.end(),[&](const auto& e){return e.id==id;});
    return it==s.entities.end()?nullptr:&*it;
}
const w::House& house(const w::Snapshot& s,std::uint32_t id) {
    const auto it=std::find_if(s.houses.begin(),s.houses.end(),[&](const auto& h){return h.id==id;});
    if (it==s.houses.end()) throw std::runtime_error("Match house is missing");
    return *it;
}
void pixel(render::Bitmap& image,int x,int y,std::array<std::uint8_t,3> color) {
    if (x<0 || y<0 || x>=image.width || y>=image.height) return;
    const auto at=(std::size_t(y)*image.width+x)*4;
    for (int c=0;c<3;++c) image.rgba[at+c]=color[c];
    image.rgba[at+3]=255;
}
void rectangle(render::Bitmap& image,int x,int y,int width,int height,std::array<std::uint8_t,3> color) {
    for (int i=0;i<width;++i) {pixel(image,x+i,y,color);pixel(image,x+i,y+height-1,color);}
    for (int i=0;i<height;++i) {pixel(image,x,y+i,color);pixel(image,x+width-1,y+i,color);}
}
std::string result_name(sim::Victory result) {
    switch (result) {
    case sim::Victory::PlayerWon:return "VICTORY";
    case sim::Victory::PlayerLost:return "DEFEAT";
    case sim::Victory::Draw:return "DRAW";
    default:return {};
    }
}
struct CursorScope {
    int previous=SDL_ShowCursor(SDL_QUERY);
    CursorScope(){SDL_ShowCursor(SDL_DISABLE);}
    ~CursorScope(){SDL_ShowCursor(previous);}
};

// TODO-ORIGINAL: Native match orchestration. See RA1 CONQUER.CPP Main_Loop
// and LOGIC.CPP for the input, object, factory and house boundaries. Original
// base RA2 collection ordering, missions and event records remain separate.
class Battle final : public loop::MessagePump, public loop::Renderer {
    const BattleOptions& args_;
    w::BaseBattleAssets& assets_;
    w::Snapshot world_; // The single owning session world.
    rules::IniFile rules_;
    ra2text::CsfFile labels_;
    p::Catalog catalog_;
    p::FactorySet factories_;
    sim::PrototypeMovement movement_;
    sim::PrototypeCombatRuntime combat_;
    sim::PrototypeEconomy economy_;
    sim::PrototypeAI ai_;
    sim::VictoryState victory_;
    sim::VictoryRules victory_rules_;
    Window& window_;
    Scene& scene_;
    const render::Bitmap& terrain_;
    render::MapGeometry geometry_;
    loop::Clock& clock_;
    std::uint32_t player_,enemy_;
    BattleOrders orders_;
    BattleHud hud_;
    BattleCursor cursor_;
    BattleEffects effects_;
    BattleAudio audio_;
    CursorScope cursor_scope_;
    BattleHelp help_;
    ui::ScoreAssets score_assets_;
    std::optional<BattleScore> score_;
    std::optional<gfx::RgbaImage> score_image_;
    HudState hud_state_;
    render::Bitmap frame_;
    std::optional<SceneFrame> initial_scene_;
    int initial_camera_x_=0,initial_camera_y_=0;
    render::RadarStyle radar_style_;
    std::map<std::uint32_t,std::uint32_t> attacks_;
    std::map<std::uint32_t,int> power_health_;
    std::map<std::uint32_t,ra2map::Cell> manual_moves_;
    std::set<std::uint32_t> move_destinations_;
    std::vector<Order> pending_;
    BattleBuildQueue build_queue_;
    std::set<std::int32_t> keys_;
    std::set<std::string> gaps_;
    int camera_x_=0,camera_y_=0,pointer_x_=-1,pointer_y_=-1;
    BattleStatus status_;
    SidebarChoices sidebar_choices_;
    CameraScroll camera_scroll_;
    std::string placement_;
    bool quit_=false,autoplay_moved_=false,autoplay_deployed_=false;
    bool help_visible_=true,show_score_=false;
    enum class PausePanel { Closed, Options, Abort };
    PausePanel pause_panel_=PausePanel::Closed;
    bool force_draw_=false;
    int human_stage_=0;
    std::uint32_t human_target_=0,human_wait_frame_=0;
    std::map<std::uint32_t,sim::Coord> human_positions_;
    int human_camera_=0;
    int human_cash_before_=0,human_hold_cash_=0,human_hold_progress_=0;
    std::uint32_t human_product_=0;
    bool ai_opening_check_=std::getenv("RA2_AI_OPENING_CHECK")!=nullptr;
    std::optional<std::uint32_t> first_ai_shot_;
    ui::Opponent difficulty_=ui::Opponent::Easy;
    std::uint32_t autoplay_unit_=0;
    std::uint64_t input_frame_=0;
    std::optional<sim::Coord> autoplay_start_;
public:
    std::size_t presented=0,moved=0,fired=0,impacts=0,built=0,deployed=0,ai_orders=0,infantry=0;
    Battle(const BattleOptions& args,w::BaseBattleAssets& assets,w::Snapshot world,Window& window,
           Scene& scene,const render::Bitmap& terrain,loop::Clock& clock,const fs::MixFileSystem& files)
        :args_(args),assets_(assets),world_(std::move(world)),
         rules_(overlay_rules(overlay_rules(assets.rules(),assets.battle_rules()),assets.map_ini())),
         catalog_(p::Catalog::from(rules_,assets.art_ini(),assets.map_ini())),
         combat_(assets.rules(),assets.battle_rules(),assets.map_ini(),assets.art_ini(),
                 (args.game_directory/"game.exe").string()),economy_(rules_),ai_(rules_),
         window_(window),scene_(scene),terrain_(terrain),geometry_(render::MapGeometry::from_ini(assets.map_ini())),
         clock_(clock),player_(world_.houses.at(0).id),enemy_(world_.houses.at(1).id),orders_(player_),cursor_(files),effects_(files),
         audio_(files,rules_,player_,rules_.read_string(world_.houses.at(0).country,"Side")=="GDI"?"Allied":"Russian",
                !args.headless,16*frame_delay_ticks(args)) {
        if(const auto labels=assets_.read_asset("RA2.CSF"))labels_.parse(labels->data(),labels->size());
        movement_.use_rules(rules_);
        score_assets_=ui::ScoreAssets::load(files,labels_);
        score_.emplace(std::vector<std::pair<std::uint32_t,std::string>>{{player_,house(world_,player_).name},{enemy_,house(world_,enemy_).name}});
        help_visible_=!args_.autoplay;
        status("Select MCV. D deploys.");
        camera_scroll_.reset(clock_.now_ms());
        for(const auto& slot:args_.setup.opponents)if(slot.opponent && *slot.opponent!=ui::Opponent::Closed) {
            difficulty_=*slot.opponent;break;
        }
        const auto ai_bytes=files.read_file("AI.INI");
        if(!ai_bytes)throw std::runtime_error("Base AI.INI is missing");
        rules::IniFile ai_ini;
        ai_ini.parse(std::string_view(reinterpret_cast<const char*>(ai_bytes->data()),ai_bytes->size()));
        const auto ai_difficulty=difficulty_==ui::Opponent::Hard?sim::PrototypeAI::Difficulty::Hard:
            difficulty_==ui::Opponent::Medium?sim::PrototypeAI::Difficulty::Normal:sim::PrototypeAI::Difficulty::Easy;
        ai_=sim::PrototypeAI(rules_,&ai_ini,ai_difficulty);
        if(!ai_.uses_ai_ini())throw std::runtime_error("Base AI.INI has no usable team triggers");
        std::cout<<"Computer policy: base AI.INI triggers="<<ai_.trigger_count()<<" difficulty="<<int(ai_difficulty)<<'\n';
        // Base 004E4110 reads Short Game (00A3D2C2) and the first two BaseUnit types.
        victory_rules_.short_game=short_game_choice(args_.setup,rules_);
        victory_rules_.base_units=rules_.read_list("General","BaseUnit");
        // Base 004E3832 reads Super Weapons (00A3D2C3) in House::Can_Build.
        factories_.flags.super_weapons=super_weapons_choice(args_.setup,rules_);
        const auto initialized=proto::initialize(world_,factories_,catalog_,player_);
        if (initialized.status!=p::Status::Done) throw std::runtime_error(initialized.detail);
        refresh_power_damage();
        const auto start=std::find_if(world_.entities.begin(),world_.entities.end(),[&](const auto& e){return live(e)&&e.house_id==player_;});
        if (start!=world_.entities.end()) center(*start);
        radar_style_=render::battle_radar_style(rules_);
        for(const auto& h:world_.houses) if(h.selected_color && *h.selected_color>=0 && *h.selected_color<8) {
            const auto c=house_color(rules_,*h.selected_color);
            radar_style_.houses[h.id]={c.r,c.g,c.b};
        }
        effects_.observe(world_,combat_.projectiles(),0,effect_project());
        audio_.observe(world_,0);
        if(!args_.headless&&!audio_.enabled())std::cerr<<"Combat audio unavailable: "<<audio_.failure()<<'\n';
        refresh_hud();
        std::vector<BuildChoice> icons;
        for(const auto& list:{&catalog_.buildings,&catalog_.units,&catalog_.infantry,&catalog_.aircraft})
            for(const auto& type:*list)icons.push_back({type.id,type.id,type.cost,true,0});
        hud_.load_assets(files,rules_,assets.art_ini(),icons);
        // Compose the initial view while loading. The first event pump can
        // change the camera, selection or help; overlays stay live below.
        initial_camera_x_=camera_x_;initial_camera_y_=camera_y_;
        initial_scene_=scene_.render(terrain_,battle_scene_state(world_,0),{camera_x_,camera_y_,1024,768},&world_);
    }
    const w::Snapshot& world() const {return world_;}
    sim::Victory result() const {return victory_.result;}
    bool autoplay_moved() const {return autoplay_moved_;}
    std::string name(const std::string& type) const {
        if(const auto* label=labels_.find(rules_.read_string(type,"UIName")))return label->text;
        return rules_.read_string(type,"Name",type);
    }
    void pan(int dx,int dy) {
        camera_x_=std::clamp(camera_x_+dx,0,std::max(0,terrain_.width-BattleHud::sidebar_x));
        camera_y_=std::clamp(camera_y_+dy,0,std::max(0,terrain_.height-BattleHud::view_height));
    }
    std::optional<BattlePoint> anchor(const w::Entity& e) const {
        if (!live(e)) return {};
        sim::Entity drawing;
        drawing.id=e.id;drawing.cell=*e.cell;drawing.position=e.position;
        drawing.kind=e.kind==w::Kind::Structure?sim::Kind::Structure:
            e.kind==w::Kind::Infantry?sim::Kind::Infantry:sim::Kind::Unit;
        drawing.subcell=0;
        try {
            const auto p=scene_.map_anchor(drawing);
            return BattlePoint{int(p.x)-camera_x_,int(p.y)-camera_y_};
        } catch (const std::exception&) {return {};}
    }
    BattleEffects::Project effect_project() const {
        return [this](sim::Coord p)->std::optional<EffectPoint> {
            if(p.x<0||p.y<0||p.x>=512*256||p.y>=512*256)return {};
            sim::Entity e;e.kind=sim::Kind::Unit;e.position=p;
            e.cell={std::uint16_t(p.x/256),std::uint16_t(p.y/256)};
            try { const auto at=scene_.map_anchor(e);return EffectPoint{int(at.x),int(at.y)}; }
            catch(const std::exception&) {return {};}
        };
    }
    void center(const w::Entity& e) {
        const auto point=anchor(e);
        if (point) pan(point->x-BattleHud::sidebar_x/2,point->y-BattleHud::view_height/2);
    }
    std::optional<ra2map::Cell> cell_at(BattlePoint point) const {
        if (point.x<0 || point.y<0 || point.x>=BattleHud::sidebar_x || point.y>=BattleHud::view_height) return {};
        // TODO-ORIGINAL: Pick the nearest projected floor diamond. Original
        // depth-buffer picking and bridge levels must replace this adapter.
        // Same nearest-anchor rule as a scan of every floor Cell; floor
        // heights are fixed after setup, so Scene keeps a bucket index.
        return scene_.pick_cell(std::int64_t(camera_x_)+point.x,std::int64_t(camera_y_)+point.y);
    }
    BattleView view() const {
        return {{0,0,BattleHud::sidebar_x,BattleHud::view_height},
            [this](const w::Entity& e){return anchor(e);},[this](BattlePoint p){return cell_at(p);}};
    }
    bool miner(const w::Entity& e) const {return rules_.read_bool(e.type,"Harvester");}
    bool fighter(const w::Entity& e) const {
        return live(e) && (e.kind==w::Kind::Unit || e.kind==w::Kind::Infantry) &&
            !miner(e) && !rules_.read_string(e.type,"Primary").empty();
    }
    void queue(Order order) {pending_.push_back(std::move(order));}
    void status(std::string message) {status_.set(std::move(message),clock_.now_ms());}
    int factory_slot(const p::TypeFacts& type) const {
        return type.rtti==p::Rtti::BuildingType?(type.build_cat==5?5:4):
            type.rtti==p::Rtti::InfantryType?1:type.rtti==p::Rtti::UnitType?2:0;
    }
    int active_count(const std::string& type) const {
        return int(std::count_if(factories_.factories.begin(),factories_.factories.end(),[&](const auto& f){
            return f.house_id==player_ && f.object && f.object_type==type;
        }));
    }
    int active_slot_count(int slot) const {
        return int(std::count_if(factories_.factories.begin(),factories_.factories.end(),[&](const auto& factory) {
            return factory.house_id==player_ && factory.slot==slot && factory.object;
        }));
    }
    void refresh_power_damage() {
        for(const auto& e:world_.entities) {
            if(e.kind!=w::Kind::Structure)continue;
            const auto* type=catalog_.find(e.type);if(!type || type->power_output<=0)continue;
            const int hp=e.resolved_hp.value_or(0);
            const auto [at,inserted]=power_health_.try_emplace(e.id,hp);
            if(!inserted && at->second!=hp) {
                at->second=hp;
                if(const auto dirty=factories_.power_dirty.find(e.house_id);dirty!=factories_.power_dirty.end())
                    dirty->second=true;
            }
        }
    }
    void run_build_queue(std::uint32_t frame) {
        for(const auto& [slot,line]:build_queue_.slots()) {
            if(line.empty())continue;
            if(std::any_of(factories_.factories.begin(),factories_.factories.end(),[&](const auto& f){
                return f.house_id==player_ && f.slot==slot && f.object;
            }))continue;
            const auto type=line.front();
            if(!sim::tech::can_build(rules_,world_,player_,type)) {build_queue_.pop(slot);continue;}
            const auto outcome=proto::begin_production(world_,factories_,catalog_,player_,type,frame);
            if(outcome.status==p::Status::Done) {
                ++built;build_queue_.pop(slot);note(outcome,"Building "+name(type)+".");
            }
        }
    }
    void deploy_selected() {
        for (const auto id:orders_.selected_ids()) queue({OrderKind::Deploy,player_,id,0,{},{}});
    }
    ra2map::Cell formation_cell(ra2map::Cell goal,std::uint32_t actor) {
        return reserve_formation_cell(world_,goal,actor,manual_moves_,move_destinations_);
    }
    void input(const Input& input) {
        if (input.kind==InputKind::Quit) {quit_=true;return;}
        if (input.kind==InputKind::Focus || input.kind==InputKind::Resize) {
            if(input.kind==InputKind::Focus && !input.pressed) {
                keys_.clear();pointer_x_=pointer_y_=-1;
            }
            // Modal panels must not swallow the focus gain that restores orders.
            (void)orders_.handle(input,world_,view(),clock_.now_ms());
            return;
        }
        if (input.kind==InputKind::Motion || input.kind==InputKind::Button) {
            pointer_x_=input.x;pointer_y_=input.y;
        }
        if (input.kind==InputKind::Key) {
            if (input.pressed) keys_.insert(input.key);else keys_.erase(input.key);
            if (input.pressed && input.key==SDLK_ESCAPE) {
                orders_.cancel_input();keys_.clear();
                if(pause_panel_==PausePanel::Abort)pause_panel_=PausePanel::Options;
                else if(pause_panel_==PausePanel::Options)pause_panel_=PausePanel::Closed;
                else if (!placement_.empty()&&!help_visible_) {placement_.clear();orders_.set_placement_active(false);status("");}
                else if(help_visible_)help_visible_=false;
                else if(victory_.settled())quit_=true;
                else pause_panel_=PausePanel::Options;
                return;
            }
            if(pause_panel_==PausePanel::Closed && input.pressed && input.key==SDLK_F1) {
                orders_.cancel_input();keys_.clear();help_visible_=!help_visible_;return;
            }
            if(input.pressed && input.key==SDLK_TAB && victory_.settled()) {show_score_=!show_score_;return;}
            if (pause_panel_==PausePanel::Closed && input.pressed && input.key==SDLK_h) {
                for (const auto& e:world_.entities) if (live(e)&&e.house_id==player_) {center(e);break;}
            }
        }
        if(pause_panel_!=PausePanel::Closed) {
            PauseAction action=PauseAction::None;
            if(input.kind==InputKind::Button && input.pressed && input.key==SDL_BUTTON_LEFT)
                action=BattleHelp::pause_action(input.x,input.y,pause_panel_==PausePanel::Abort);
            if(input.kind==InputKind::Key && input.pressed) {
                if(input.key==SDLK_q && pause_panel_==PausePanel::Options)action=PauseAction::Abort;
                if(input.key==SDLK_RETURN)action=pause_panel_==PausePanel::Abort?PauseAction::Abort:PauseAction::Resume;
            }
            if(action==PauseAction::Abort) {
                if(pause_panel_==PausePanel::Abort)quit_=true;
                else pause_panel_=PausePanel::Abort;
            } else if(action==PauseAction::Back)pause_panel_=PausePanel::Options;
            else if(action==PauseAction::Resume) {pause_panel_=PausePanel::Closed;orders_.cancel_input();keys_.clear();}
            return;
        }
        if(help_visible_) {
            if(input.kind==InputKind::Button && input.pressed)help_visible_=false;
            return;
        }
        if(show_score_) {
            if(input.kind==InputKind::Button && input.pressed)show_score_=false;
            return;
        }
        if (input.kind==InputKind::Button && input.x>=BattleHud::sidebar_x) {
            if (input.pressed && (input.key==SDL_BUTTON_LEFT || input.key==SDL_BUTTON_RIGHT)) {
                refresh_hud();
                const auto action=hud_.handle_click(input.x,input.y,hud_state_);
                if(action.kind==HudActionKind::Build && !victory_.settled()) {
                    const auto* type=catalog_.find(action.type);
                    if(!type)return;
                    const p::FactoryState* product=nullptr;
                    for(const auto& factory:factories_.factories)
                        if(factory.house_id==player_ && factory.object && factory.object_type==action.type)product=&factory;
                    const bool queueable=type->rtti!=p::Rtti::BuildingType;
                    const auto click=cameo_click(input.key==SDL_BUTTON_RIGHT,product!=nullptr,
                        product&&product->progress==54,product&&product->suspended,
                        build_queue_.count(action.type)>0,queueable);
                    switch(click) {
                    case CameoClick::RemoveQueued:
                        if(build_queue_.cancel(factory_slot(*type),action.type))status("Queued product removed.");
                        break;
                    case CameoClick::Hold:
                        note(proto::hold_production(world_,factories_,catalog_,player_,action.type,input_frame_),"On hold.");
                        break;
                    case CameoClick::Cancel: {
                        const auto outcome=proto::cancel_production(world_,factories_,catalog_,player_,action.type,input_frame_);
                        if(outcome.status==p::Status::Done && placement_==action.type) {
                            placement_.clear();orders_.set_placement_active(false);
                        }
                        note(outcome,"Production cancelled.");
                        break;
                    }
                    case CameoClick::Resume:
                        note(proto::begin_production(world_,factories_,catalog_,player_,action.type,input_frame_),"Production resumed.");
                        break;
                    case CameoClick::Place:
                        placement_=action.type;orders_.set_placement_active(true);
                        status("Place "+name(placement_)+". Esc cancels.");
                        break;
                    case CameoClick::Queue:
                        if(build_queue_.add(factory_slot(*type),action.type,active_slot_count(factory_slot(*type))))
                            status("Queued "+name(action.type)+".");
                        else status("Queue full: thirty per factory.");
                        break;
                    case CameoClick::Begin:
                        queue({OrderKind::Build,player_,0,0,action.type,{}});
                        break;
                    case CameoClick::None:break;
                    }
                } else if(input.key==SDL_BUTTON_RIGHT)return;
                else if(action.kind==HudActionKind::Deploy && !victory_.settled())deploy_selected();
                else if (action.kind==HudActionKind::Minimap) {
                    pan(action.position.x-camera_x_-BattleHud::sidebar_x/2,
                        action.position.y-camera_y_-BattleHud::view_height/2);
                }
            }
            return;
        }
        if (!placement_.empty() && input.kind==InputKind::Button && input.pressed) {
            if (input.key==SDL_BUTTON_RIGHT) {placement_.clear();orders_.set_placement_active(false);}
            else if (input.key==SDL_BUTTON_LEFT) if (const auto cell=cell_at({input.x,input.y})) {
                const auto origin=proto::placement_origin(catalog_,placement_,{std::int16_t(cell->x),std::int16_t(cell->y)});
                if(origin && origin->x>=0 && origin->y>=0)
                    queue({OrderKind::Place,player_,0,0,placement_,
                        {std::uint16_t(origin->x),std::uint16_t(origin->y)}});
                else status("Can't build here.");
            }
            return;
        }
        auto commands=orders_.handle(input,world_,view(),clock_.now_ms());
        if(!victory_.settled())pending_.insert(pending_.end(),commands.begin(),commands.end());
    }
    void pump(loop::State& state) override {
        input_frame_=state.frame_counter;
        if(args_.human_input_check)human_input(state);
        for (const auto& event:window_.poll()) {
            if (!args_.headless && event.kind==InputKind::Focus) state.window_active=event.pressed;
            input(event);
        }
        state.paused=help_visible_||pause_panel_!=PausePanel::Closed;
        score_->clock(clock_.now_ms(),!state.paused&&state.window_active&&!victory_.settled());
        audio_.service(state.paused||!state.window_active);
        if (quit_) state.running=false;
    }
    void test_click(int x,int y,int button=SDL_BUTTON_LEFT) {
        window_.inject_for_test({InputKind::Motion,x,y});
        window_.inject_for_test({InputKind::Button,x,y,button,true});
        window_.inject_for_test({InputKind::Button,x,y,button,false});
    }
    void test_key(int key) {
        window_.inject_for_test({InputKind::Key,0,0,key,true});
        window_.inject_for_test({InputKind::Key,0,0,key,false});
    }
    void test_pan_to(BattlePoint point) {
        for(const auto& [key,held]:std::vector<std::pair<int,bool>>{
            {SDLK_LEFT,point.x<40},{SDLK_RIGHT,point.x>760},
            {SDLK_UP,point.y<40},{SDLK_DOWN,point.y>720}})
            if(bool(keys_.count(key))!=held)window_.inject_for_test({InputKind::Key,0,0,key,held});
    }
    void checkpoint(const loop::State& state,const std::string& label) {
        force_draw_=true;present(state);force_draw_=false;
        int width=0,height=0;const auto pixels=window_.capture(width,height);
        if(args_.headless&&(width!=frame_.width||height!=frame_.height||pixels!=frame_.rgba))
            throw std::runtime_error("Human input capture differs: "+label);
        if(const auto* path=std::getenv("RA2_MATCH_CAPTURE")) {
            auto prefix=std::filesystem::path(path);prefix.replace_extension();
            render::write_ppm(frame_,prefix.string()+"-"+label+".ppm");
        }
        std::cerr<<"Human input checkpoint: "<<label<<" frame="<<state.frame_counter<<'\n';
    }
    void human_input(const loop::State& state) {
        const auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        const auto own_type=[&](const std::string& type)->const w::Entity* {
            for(const auto& e:world_.entities)if(live(e)&&e.house_id==player_&&e.type==type)return &e;
            return nullptr;
        };
        const auto palette_click=[&](const std::string& type,int button=SDL_BUTTON_LEFT) {
            refresh_hud();
            const auto it=std::find_if(hud_state_.choices.begin(),hud_state_.choices.end(),[&](const auto& c){return c.type==type;});
            require(it!=hud_state_.choices.end(),"Human input: power plant cameo is missing");
            const auto r=BattleHud::build_button_bounds(int(it-hud_state_.choices.begin()));
            test_click(r.x+8,r.y+8,button);
        };
        const bool allied=house(world_,player_).side_index.value_or(0)==0;
        const std::string power=allied?"GAPOWR":"NAPOWR";
        switch(human_stage_) {
        case 0:
            require(help_visible_,"Human input: start help was not shown");
            checkpoint(state,"00-help");
            window_.inject_for_test({InputKind::Focus,0,0,0,true});
            test_click(500,500);human_stage_=1;break;
        case 1:
            for(const auto& e:world_.entities)if(live(e)&&e.house_id==player_&&!rules_.read_string(e.type,"DeploysInto").empty()) {
                const auto at=anchor(e);require(at.has_value(),"Human input: MCV has no screen anchor");
                test_click(at->x,at->y);
                const char* mode=std::getenv("RA2_INPUT_DEPLOY");
                if(mode && std::string(mode)!="double") {
                    // Exercise pause and focus through SDL, before the deploy event.
                    test_key(SDLK_F1);
                    test_key(SDLK_d); // Help consumes commands while paused.
                    const auto button=BattleHud::deploy_bounds();
                    test_click(button.x+8,button.y+8); // Dismiss help without deploying.
                    window_.inject_for_test({InputKind::Focus,0,0,0,false});
                    window_.inject_for_test({InputKind::Focus,0,0,0,true});
                    if(std::string(mode)=="sidebar")test_click(button.x+8,button.y+8);
                    else test_key(SDLK_d);
                } else test_click(at->x,at->y);
                human_stage_=2;return;
            }
            throw std::runtime_error("Human input: MCV is missing");
        case 2:
            require(own_type(allied?"GACNST":"NACNST"),"Human input: MCV input did not deploy");
            human_cash_before_=house(world_,player_).cash.value_or(0);
            checkpoint(state,"01-deployed");palette_click(power);human_stage_=3;break;
        case 3: {
            const p::FactoryState* product=nullptr;
            for(const auto& f:factories_.factories)if(f.house_id==player_&&f.object&&f.object_type==power)product=&f;
            require(product,"Human input: sidebar click did not begin power production");
            if(product->progress<2)break;
            checkpoint(state,"02-production");
            human_product_=product->object;human_hold_progress_=product->progress;
            human_hold_cash_=house(world_,player_).cash.value_or(0);
            human_wait_frame_=state.frame_counter;
            palette_click(power,SDL_BUTTON_RIGHT);human_stage_=80;break;
        }
        case 80: {
            const auto product=std::find_if(factories_.factories.begin(),factories_.factories.end(),[&](const auto& f){return f.object==human_product_;});
            require(product!=factories_.factories.end() && product->suspended && product->progress==human_hold_progress_,"Human input: right click did not hold production");
            require(house(world_,player_).cash==human_hold_cash_,"Human input: held production spent credits");
            if(state.frame_counter-human_wait_frame_<4)break;
            palette_click(power);human_stage_=81;break;
        }
        case 81: {
            const auto product=std::find_if(factories_.factories.begin(),factories_.factories.end(),[&](const auto& f){return f.object==human_product_;});
            require(product!=factories_.factories.end() && !product->suspended && product->progress==human_hold_progress_ && product->timer_duration>0,"Human input: left click did not resume held production");
            palette_click(power,SDL_BUTTON_RIGHT);human_stage_=82;break;
        }
        case 82: {
            const auto product=std::find_if(factories_.factories.begin(),factories_.factories.end(),[&](const auto& f){return f.object==human_product_;});
            require(product!=factories_.factories.end() && product->suspended,"Human input: resumed product did not hold again");
            palette_click(power,SDL_BUTTON_RIGHT);human_stage_=83;break;
        }
        case 83:
            require(active_count(power)==0 && house(world_,player_).cash==human_cash_before_,"Human input: second right click did not cancel and refund");
            palette_click(power);human_stage_=84;break;
        case 84:
            require(active_count(power)==1,"Human input: fresh production failed after cancel");
            std::cerr<<"Human production hold, resume and refund passed\n";
            human_stage_=4;break;
        case 4:
            for(const auto& f:factories_.factories)if(f.house_id==player_&&f.object_type==power&&f.progress==54) {
                checkpoint(state,"03-ready");palette_click(power);human_stage_=5;break;
            }
            break;
        case 5: {
            require(orders_.placement_active(),"Human input: READY did not start placement");
            const auto* base=own_type(allied?"GACNST":"NACNST");require(base,"Human input: yard vanished");
            for(int r=2;r<=12;++r)for(int dy=-r;dy<=r;++dy)for(int dx=-r;dx<=r;++dx) {
                if(std::max(std::abs(dx),std::abs(dy))!=r)continue;
                const p::CellXY cell{std::int16_t(int(base->cell->x)+dx),std::int16_t(int(base->cell->y)+dy)};
                // A private candidate only chooses a legal click position.
                // The real world changes solely through queued window events.
                auto candidate=world_;auto factories=factories_;
                if(proto::place(candidate,factories,catalog_,player_,power,cell,state.frame_counter).status!=p::Status::Done)continue;
                w::Entity mark;mark.lifetime=w::Lifetime::Placed;mark.resolved_hp=1;
                const auto shift=proto::placement_origin(catalog_,power,{0,0});
                require(shift.has_value(),"Human input: missing placement cursor dimensions");
                const auto mouse_x=cell.x-shift->x,mouse_y=cell.y-shift->y;
                mark.cell=ra2map::Cell{std::uint16_t(mouse_x),std::uint16_t(mouse_y)};
                const auto found=world_.cells.find(std::uint32_t(mouse_y)*512+mouse_x);
                if(found==world_.cells.end() || !found->second.terrain)continue;
                mark.position=sim::Coord{mouse_x*256+128,mouse_y*256+128,found->second.terrain->floor_world_height.value_or(0)};
                const auto at=anchor(mark);if(!at||at->x<0||at->x>=804||at->y<0||at->y>=768)continue;
                test_click(at->x,at->y);human_stage_=6;return;
            }
            throw std::runtime_error("Human input: no visible legal power placement");
        }
        case 6: {
            require(own_type(power),"Human input: map click did not place power plant");
            checkpoint(state,"04-placed");
            int left=804,top=768,right=0,bottom=0;
            for(const auto& e:world_.entities)if(e.house_id==player_&&fighter(e))if(const auto at=anchor(e)) {
                left=std::min(left,at->x-12);top=std::min(top,at->y-12);
                right=std::max(right,at->x+12);bottom=std::max(bottom,at->y+12);
                human_positions_[e.id]=*e.position;
            }
            require(human_positions_.size()>=2,"Human input: two starting tanks are missing");
            window_.inject_for_test({InputKind::Button,left,top,SDL_BUTTON_LEFT,true});
            window_.inject_for_test({InputKind::Motion,right,bottom});
            window_.inject_for_test({InputKind::Button,right,bottom,SDL_BUTTON_LEFT,false});human_stage_=7;break;
        }
        case 7: {
            require(orders_.selected_ids().size()>=2,"Human input: tank box selection failed");
            checkpoint(state,"05-selected");
            human_wait_frame_=state.frame_counter;
            test_key(SDLK_ESCAPE);human_stage_=72;break;
        }
        case 72:
            require(pause_panel_==PausePanel::Options,"Human input: Esc did not show pause options");
            require(state.frame_counter==human_wait_frame_,"Human input: options did not pause play");
            test_key(SDLK_q);human_stage_=73;break;
        case 73:
            require(pause_panel_==PausePanel::Abort,"Human input: abort confirmation is missing");
            test_key(SDLK_ESCAPE);human_stage_=74;break;
        case 74:
            require(pause_panel_==PausePanel::Options,"Human input: abort cancel did not return to options");
            test_key(SDLK_ESCAPE);human_stage_=75;break;
        case 75:
            require(pause_panel_==PausePanel::Closed,"Human input: options did not resume play");
            human_wait_frame_=state.frame_counter;
            window_.inject_for_test({InputKind::Focus,0,0,0,false});
            test_key(SDLK_F1);human_stage_=70;break;
        case 70:
            require(help_visible_,"Human input: F1 did not open help during play");
            require(state.frame_counter==human_wait_frame_,"Human input: help did not pause the simulation");
            window_.inject_for_test({InputKind::Focus,0,0,0,true});
            test_key(SDLK_F1);human_stage_=71;break;
        case 71: {
            require(!help_visible_,"Human input: F1 did not close help during play");
            require(orders_.selected_ids().size()>=2,"Human input: modal panels lost the selection");
            const auto* e=find(world_,human_positions_.begin()->first);require(e,"Human input: tank vanished");
            for(int dy=-6;dy<=6;++dy)for(int dx=3;dx<=7;++dx) {
                auto mark=*e;mark.cell=ra2map::Cell{std::uint16_t(int(e->cell->x)+dx),std::uint16_t(int(e->cell->y)+dy)};
                const auto cell=world_.cells.find(std::uint32_t(mark.cell->y)*512+mark.cell->x);
                if(cell==world_.cells.end()||!cell->second.heads||cell->second.heads->ground_head||cell->second.unit_land_speed.value_or(0)<=0)continue;
                mark.position=sim::Coord{int(mark.cell->x)*256+128,int(mark.cell->y)*256+128,cell->second.terrain->floor_world_height.value_or(0)};
                const auto at=anchor(mark);if(!at||at->x<20||at->x>=780||at->y<20||at->y>=740)continue;
                test_click(at->x,at->y);human_wait_frame_=state.frame_counter;human_stage_=8;return;
            }
            throw std::runtime_error("Human input: no visible move destination");
        }
        case 8: {
            bool moved_all=true;
            for(const auto& [id,before]:human_positions_) {
                const auto* e=find(world_,id);
                moved_all &= e&&e->position&&(e->position->x!=before.x||e->position->y!=before.y);
            }
            require(state.frame_counter-human_wait_frame_<300,"Human input: selected tanks did not move");
            if(!moved_all)break;
            checkpoint(state,"06-moved");
            for(const auto& e:world_.entities)if(live(e)&&e.house_id==enemy_&&fighter(e)) {
                human_target_=e.id;
                const auto at=anchor(e);require(at.has_value(),"Human input: enemy has no anchor");
                // No radar exists in this opening. Pan through real key events.
                test_pan_to(*at);human_wait_frame_=state.frame_counter;
                human_stage_=9;return;
            }
            throw std::runtime_error("Human input: enemy tank is missing");
        }
        case 9: {
            const auto* e=find(world_,human_target_);require(e,"Human input: target vanished");
            const auto at=anchor(*e);require(at.has_value(),"Human input: target has no anchor");
            require(state.frame_counter-human_wait_frame_<1200,"Human input: camera did not reach enemy");
            if(at->x<40||at->x>760||at->y<40||at->y>720) {test_pan_to(*at);break;}
            test_pan_to({400,380});
            test_click(at->x,at->y);human_stage_=10;break;
        }
        case 10:
            if(fired) {checkpoint(state,"07-attack");human_stage_=11;}
            break;
        case 11:
            if(victory_.settled()) {
                require(show_score_&&score_image_,"Human input: result screen is missing");
                checkpoint(state,"08-result");test_key(SDLK_TAB);human_camera_=camera_x_;
                human_wait_frame_=state.frame_counter;
                window_.inject_for_test({InputKind::Key,0,0,camera_x_>0?SDLK_LEFT:SDLK_RIGHT,true});
                human_stage_=12;
            } else if(state.frame_counter%90==0&&!orders_.selected_ids().empty()) {
                const auto* target=find(world_,human_target_);
                if(!target||!live(*target)) {
                    target=nullptr;
                    for(const auto& e:world_.entities)if(live(e)&&e.house_id==enemy_) {
                        human_target_=e.id;target=&e;break;
                    }
                }
                // Continue through window input after winning the first fight.
                // Remaining enemies can be outside the current camera view.
                if(target)if(const auto at=anchor(*target)) {
                    if(at->x<40||at->x>760||at->y<40||at->y>720)test_pan_to(*at);
                    else {test_pan_to({400,380});test_click(at->x,at->y);}
                }
            }
            break;
        case 12:
            require(state.frame_counter-human_wait_frame_<30,"Human input: camera stopped after game over");
            if(camera_x_==human_camera_)break;
            window_.inject_for_test({InputKind::Key,0,0,SDLK_LEFT,false});
            window_.inject_for_test({InputKind::Key,0,0,SDLK_RIGHT,false});
            checkpoint(state,"09-camera-after-result");test_key(SDLK_ESCAPE);human_stage_=13;break;
        default:break;
        }
    }
    void note(const p::Outcome& outcome,const std::string& success,const std::string& failure="Cannot complete this order.") {
        status(production_status(outcome.status,success,failure));
        if (outcome.status==p::Status::NeedsInput || outcome.status==p::Status::Unsupported)
            gaps_.insert(outcome.detail);
    }
    void execute(const Order& order,std::uint32_t frame) {
        loop::profile::Timer probe(loop::profile::Phase::Orders);
        if (order.house!=player_ && order.house!=enemy_) return;
        const auto* e=find(world_,order.entity);
        if (order.kind!=OrderKind::Build && order.kind!=OrderKind::Place &&
            (!e || !live(*e) || e->house_id!=order.house)) return;
        switch (order.kind) {
        case OrderKind::Move:
            if (miner(*e)) {status("Miners harvest automatically.");break;}
            combat_.stop(world_,order.entity);attacks_.erase(order.entity);
            {
                const auto goal=formation_cell(order.cell,order.entity);
                const auto move=movement_.issue(world_,order.entity,goal,
                    sim::original_rule_speed(rules_.read_integer(e->type,"Speed",4)));
                if(move.status==sim::PrototypeMoveStatus::Complete)manual_moves_[order.entity]=goal;
                if (order.house==player_) status(move.status==sim::PrototypeMoveStatus::Complete?"Moving.":"Cannot move there.");
            }
            break;
        case OrderKind::Attack:
            manual_moves_.erase(order.entity);
            if (combat_.attack(world_,order.entity,order.target)) {
                const auto prior=attacks_.find(order.entity);
                if(prior==attacks_.end()||prior->second!=order.target)movement_.cancel(world_,order.entity);
                attacks_[order.entity]=order.target;
            }
            break;
        case OrderKind::Stop:
            manual_moves_.erase(order.entity);
            movement_.cancel(world_,order.entity);combat_.stop(world_,order.entity);attacks_.erase(order.entity);break;
        case OrderKind::Deploy: {
            manual_moves_.erase(order.entity);
            movement_.cancel(world_,order.entity);combat_.stop(world_,order.entity);attacks_.erase(order.entity);
            const auto outcome=proto::deploy(world_,factories_,catalog_,order.entity,true);
            if (outcome.status==p::Status::Done) ++deployed;
            if (order.house==player_) note(outcome,"Construction yard deployed. Choose a building in the sidebar.","Cannot deploy here.");
            break;
        }
        case OrderKind::Build: {
            // TODO-ORIGINAL: One product per factory slot. Repeated clicks
            // cannot restart its timer or spend money twice.
            const auto* type=catalog_.find(order.type);
            if (!type) break;
            const int slot=type->rtti==p::Rtti::BuildingType?(type->build_cat==5?5:4):
                type->rtti==p::Rtti::InfantryType?1:type->rtti==p::Rtti::UnitType?2:0;
            if (std::any_of(factories_.factories.begin(),factories_.factories.end(),[&](const auto& f){
                return f.house_id==order.house && f.slot==slot && f.object;
            })) break;
            const auto outcome=proto::begin_production(world_,factories_,catalog_,order.house,order.type,frame);
            if (outcome.status==p::Status::Done) ++built;
            if (order.house==player_) note(outcome,"Building "+name(order.type)+".");
            break;
        }
        case OrderKind::Place: {
            const auto proximity=proto::passes_proximity(world_,factories_,catalog_,order.house,order.type,
                {std::int16_t(order.cell.x),std::int16_t(order.cell.y)});
            if (!proximity || !*proximity) {if(order.house==player_)status("Place this building closer to your base.");break;}
            const auto outcome=proto::place(world_,factories_,catalog_,order.house,order.type,
                {std::int16_t(order.cell.x),std::int16_t(order.cell.y)},frame);
            if (outcome.status==p::Status::Done && order.house==player_) {
                placement_.clear();orders_.set_placement_active(false);
            }
            if(order.house==player_)note(outcome,"Building placed.","Can't build here.");
            break;
        }
        }
    }
    void auto_place(std::uint32_t owner,std::uint32_t frame) {
        loop::profile::Timer probe(loop::profile::Phase::AutoPlace);
        std::vector<std::string> ready;
        for (const auto& f:factories_.factories) if (f.house_id==owner && f.object && f.progress==54) {
            const auto* type=catalog_.find(f.object_type);
            if (type && type->rtti==p::Rtti::BuildingType) ready.push_back(f.object_type);
        }
        for (const auto& type:ready) {
            std::vector<ra2map::Cell> bases;
            for (const auto& e:world_.entities) if (live(e)&&e.house_id==owner&&e.kind==w::Kind::Structure) bases.push_back(*e.cell);
            bool placed=false;
            for (const auto base:bases) {
                for (int radius=2;radius<=12 && !placed;++radius) for (int dy=-radius;dy<=radius && !placed;++dy)
                    for (int dx=-radius;dx<=radius && !placed;++dx) {
                        if (std::max(std::abs(dx),std::abs(dy))!=radius) continue;
                        const p::CellXY cell{std::int16_t(int(base.x)+dx),std::int16_t(int(base.y)+dy)};
                        // place() checks proximity before its first write.
                        placed=proto::place(world_,factories_,catalog_,owner,type,cell,frame).status==p::Status::Done;
                    }
                if(placed)break;
            }
        }
    }
    void autoplay(std::uint32_t frame) {
        loop::profile::Timer probe(loop::profile::Phase::Autoplay);
        if (frame==0) {
            for (const auto& e:world_.entities) if (live(e)&&e.house_id==player_&&
                !rules_.read_string(e.type,"DeploysInto").empty()) {
                center(e);
                const auto a=anchor(e);
                if(a) {
                    input({InputKind::Button,a->x,a->y,SDL_BUTTON_LEFT,true});
                    input({InputKind::Button,a->x,a->y,SDL_BUTTON_LEFT,false});
                    input({InputKind::Key,0,0,SDLK_d,true});
                    input({InputKind::Key,0,0,SDLK_d,false});
                    autoplay_deployed_=true;
                }
                break;
            }
        }
        if (frame==1 && !ai_opening_check_) {
            for (const auto& e:world_.entities) if (e.house_id==player_&&fighter(e)) {
                autoplay_unit_=e.id;autoplay_start_=e.position;center(e);
                const auto a=anchor(e);
                if(a) {
                    input({InputKind::Button,a->x,a->y,SDL_BUTTON_LEFT,true});
                    input({InputKind::Button,a->x,a->y,SDL_BUTTON_LEFT,false});
                    for (const int delta:{3,-3,5,-5}) {
                        auto destination=e;
                        destination.cell=ra2map::Cell{std::uint16_t(int(e.cell->x)+delta),e.cell->y};
                        destination.position=sim::Coord{int(destination.cell->x)*256+128,e.position->y,e.position->z};
                        const auto b=anchor(destination);
                        if(b && b->x>=0&&b->x<BattleHud::sidebar_x&&b->y>=0&&b->y<768) {
                            input({InputKind::Button,b->x,b->y,SDL_BUTTON_LEFT,true});
                            input({InputKind::Button,b->x,b->y,SDL_BUTTON_LEFT,false});break;
                        }
                    }
                }
                break;
            }
        }
        const auto* moving=find(world_,autoplay_unit_);
        if(moving && moving->position && autoplay_start_ &&
            (moving->position->x!=autoplay_start_->x || moving->position->y!=autoplay_start_->y)) autoplay_moved_=true;
        if(frame%30) return;
        const auto has=[&](const std::string& type){return std::any_of(world_.entities.begin(),world_.entities.end(),
            [&](const auto& e){return live(e)&&e.house_id==player_&&e.type==type;});};
        const bool allied=house(world_,player_).side_index.value_or(0)==0;
        // TODO-ORIGINAL: A deterministic native player for app regression.
        // It uses the same production and attack commands as the interface.
        const std::vector<std::string> plan=ai_opening_check_?
            std::vector<std::string>{"GAPOWR","GAREFN","GAPILE"}:allied?
            std::vector<std::string>{"GAPOWR","GAPILE","GAREFN","GAWEAP"}:
            std::vector<std::string>{"NAPOWR","NAHAND","NAREFN","NAWEAP"};
        for(const auto& type:plan) if(!has(type)) {queue({OrderKind::Build,player_,0,0,type,{}});break;}
        if(ai_opening_check_)return;
        std::size_t foot=0;
        for(const auto& e:world_.entities) if(e.house_id==player_&&live(e)&&e.kind==w::Kind::Infantry)++foot;
        if(foot<6)queue({OrderKind::Build,player_,0,0,allied?"E1":"E2",{}});
        queue({OrderKind::Build,player_,0,0,allied?"MTNK":"HTNK",{}});
        if(frame<120) return;
        std::vector<std::uint32_t> fighters;
        for(const auto& e:world_.entities) if(e.house_id==player_&&fighter(e))fighters.push_back(e.id);
        for(const auto id:fighters) {
            const auto* source=find(world_,id);const w::Entity* target=nullptr;
            std::int64_t best=std::numeric_limits<std::int64_t>::max();
            for(const auto& other:world_.entities) if(live(other)&&other.house_id==enemy_&&
                combat_.can_attack(world_,id,other.id)) {
                const auto dx=std::int64_t(source->position->x)-other.position->x;
                const auto dy=std::int64_t(source->position->y)-other.position->y;
                const auto distance=dx*dx+dy*dy;
                if(distance<best){best=distance;target=&other;}
            }
            if(target)queue({OrderKind::Attack,player_,id,target->id,{},*target->cell});
        }
    }
    void update(loop::State& state) {
        const auto frame=state.frame_counter; // All match subsystems use this frame.
        const bool edge=pointer_x_>=0 && pointer_x_<1024 && pointer_y_>=0 && pointer_y_<768;
        // Camera speed follows elapsed time, including at unlimited game speed.
        const int horizontal=int(keys_.count(SDLK_RIGHT))-int(keys_.count(SDLK_LEFT))+
            int(edge&&pointer_x_>=1016)-int(edge&&pointer_x_<8);
        const int vertical=int(keys_.count(SDLK_DOWN))-int(keys_.count(SDLK_UP))+
            int(edge&&pointer_y_>=760)-int(edge&&pointer_y_<8);
        const auto scroll=camera_scroll_.step(clock_.now_ms(),horizontal,vertical,args_.headless);
        pan(scroll.first,scroll.second);
        if(victory_.settled())return;
        run_build_queue(frame);
        if(args_.autoplay)autoplay(frame);
        auto enemy_commands=ai_.tick(world_,enemy_,player_,std::int32_t(frame));
        ai_orders+=enemy_commands.size();
        pending_.insert(pending_.end(),enemy_commands.begin(),enemy_commands.end());
        auto commands=std::move(pending_);pending_.clear();
        move_destinations_.clear();
        for(const auto& order:commands)execute(order,frame);
        if(frame%15==0){auto_place(enemy_,frame);if(args_.autoplay)auto_place(player_,frame);}
        std::map<std::uint32_t,int> cash_before;
        for(const auto& h:world_.houses)cash_before[h.id]=h.cash.value_or(0);
        economy_.tick(world_,frame);
        for(const auto& h:world_.houses)score_->harvest(h.id,cash_before[h.id],h.cash.value_or(0));
        const auto motion=movement_.tick(world_,frame);
        moved+=motion.moved.size();
        for(const auto id:motion.blocked)movement_.cancel(world_,id);
        for(const auto id:motion.arrived)manual_moves_.erase(id);
        for(auto it=manual_moves_.begin();it!=manual_moves_.end();) {
            const auto* e=find(world_,it->first);
            if(!e||!live(*e)){it=manual_moves_.erase(it);continue;}
            if(frame%15==0&&!movement_.moving(e->id))
                movement_.issue(world_,e->id,it->second,sim::original_rule_speed(rules_.read_integer(e->type,"Speed",4)));
            ++it;
        }
        if(motion.status!=sim::PrototypeMoveStatus::Complete)gaps_.insert(motion.detail);
        // Combat owns idle guard acquisition and uses each weapon's range.
        const auto fighting=combat_.tick(world_,frame);
        score_->combat(fighting.events);
        effects_.observe(world_,combat_.projectiles(),frame,effect_project(),&fighting.events,&rules_);
        for(const auto& event:fighting.events) {
            if(ai_opening_check_ && !first_ai_shot_ && event.kind==sim::PrototypeCombatEventKind::Shot &&
                event.source_house==enemy_ && event.target_house==player_) {
                first_ai_shot_=frame;
                std::cout<<"AI_OPENING difficulty="<<std::getenv("RA2_AI_OPENING_CHECK")
                    <<" speed="<<frame_delay_ticks(args_)<<" first_shot="<<frame
                    <<" source="<<event.source_type<<" target="<<event.target_type
                    <<" range_cells="<<rules_.read_double(event.weapon,"Range",0)
                    <<" dx="<<event.source_position.x-event.target_position.x
                    <<" dy="<<event.source_position.y-event.target_position.y<<'\n';
                state.running=false;
            }
            const auto* type=catalog_.find(event.target_type);
            const bool building=type&&type->rtti==p::Rtti::BuildingType;
            const auto warning=attack_warning(event,player_,name(event.target_type),name(event.source_type),building);
            if(!warning.empty())status(warning);
        }
        audio_.observe(world_,frame);
        fired+=fighting.fired;impacts+=fighting.impacts;
        for(const auto& detail:fighting.unresolved)gaps_.insert(detail);
        std::set<std::uint32_t> approach;
        for(const auto& [source,target]:fighting.out_of_range) {
            approach.insert(source);
            if(frame%15 || (movement_.moving(source)&&frame%90))continue;
            const auto* e=find(world_,source);const auto* goal=find(world_,target);
            if(e&&goal&&live(*e)&&live(*goal)&&!miner(*e)&&e->kind!=w::Kind::Structure)
                movement_.issue(world_,source,*goal->cell,sim::original_rule_speed(rules_.read_integer(e->type,"Speed",4)));
        }
        for(const auto& [source,home]:fighting.guard_return) {
            const auto* e=find(world_,source);
            if(!e||!live(*e)||miner(*e)||e->kind==w::Kind::Structure||home.x<0||home.y<0)continue;
            const ra2map::Cell goal{std::uint16_t(home.x/256),std::uint16_t(home.y/256)};
            const auto route=movement_.issue(world_,source,goal,
                sim::original_rule_speed(rules_.read_integer(e->type,"Speed",4)));
            if(route.status==sim::PrototypeMoveStatus::Complete)manual_moves_[source]=goal;
            else if(!route.detail.empty())gaps_.insert(route.detail);
        }
        for(auto it=attacks_.begin();it!=attacks_.end();) {
            const auto* source=find(world_,it->first);const auto* target=find(world_,it->second);
            if(!source||!target||!live(*source)||!live(*target)||!combat_.can_attack(world_,it->first,it->second)) {
                movement_.cancel(world_,it->first);combat_.stop(world_,it->first);it=attacks_.erase(it);
            } else {if(!approach.count(it->first))movement_.cancel(world_,it->first);++it;}
        }
        // A producer can die during combat. Abandon its orphaned products
        // before checking defeat; production timers use this same frame.
        refresh_power_damage();
        const auto after_combat=proto::tick(world_,factories_,catalog_,frame);
        if(after_combat.status!=p::Status::Done)gaps_.insert(after_combat.detail);
        sim::tech::update(rules_,world_);
        orders_.sync(world_);
        infantry=std::max(infantry,std::size_t(std::count_if(world_.entities.begin(),world_.entities.end(),[](const auto& e){return live(e)&&e.kind==w::Kind::Infantry;})));
        victory_.update(world_,player_,enemy_,victory_rules_);
        if(args_.autoplay && frame%600==0) {
            std::size_t ours=0,theirs=0;
            for(const auto& e:world_.entities)if(live(e)) {
                if(e.house_id==player_)++ours;
                if(e.house_id==enemy_)++theirs;
            }
            std::cerr<<"Match frame="<<frame<<" player="<<ours<<" computer="<<theirs
                <<" moved="<<moved<<" fired="<<fired<<" impacts="<<impacts<<" built="<<built<<'\n';
        }
        if(victory_.settled()) {
            status_.set(result_name(victory_.result)+". Press Esc to return.",clock_.now_ms(),true);
            show_score_=true;
            score_->clock(clock_.now_ms(),false);
            auto score=score_->finish(victory_.result==sim::Victory::PlayerWon);
            for(std::size_t i=0;i<score.houses.size();++i) {
                const auto id=i==0?player_:enemy_;
                for(const auto& e:world_.entities)if(e.house_id==id) {
                    if(e.kind==w::Kind::Structure)++score.houses[i].buildings_built;
                    else if(e.kind!=w::Kind::Terrain)++score.houses[i].units_built;
                }
            }
            score_assets_.continue_text="Esc: Main Menu / Click: View Battlefield / Tab: Score";
            score_image_=ui::draw_score_screen(score_assets_,score);
            window_.title("Red Alert 2 native prototype: "+status_.text(clock_.now_ms()));
            if(args_.autoplay)state.running=false;
        }
        if(world_.setup_progress!=w::SetupProgress::PrototypeComplete)throw std::runtime_error("Prototype world phase changed");
        // Setup and production commands already check their result. Run the
        // periodic diagnostic after each full interval, between AI bursts.
        if((frame%120==119 || victory_.settled())&&!w::validate(world_))throw std::runtime_error("Shared match links are invalid");
    }
    void refresh_hud() {
        loop::profile::Timer probe(loop::profile::Phase::HudRefresh);
        const auto& h=house(world_,player_);
        hud_state_.cash=h.cash.value_or(0);hud_state_.power_output=h.power_output.value_or(0);
        hud_state_.power_drain=h.power_drain.value_or(0);hud_state_.selected_labels.clear();
        hud_state_.pointer_x=pointer_x_;hud_state_.pointer_y=pointer_y_;hud_state_.can_deploy=false;
        for(const auto id:orders_.selected_ids())if(const auto* e=find(world_,id))
            if(live(*e)) {
                hud_state_.selected_labels.push_back(name(e->type)+" "+std::to_string(e->resolved_hp.value_or(0)));
                hud_state_.can_deploy|=!rules_.read_string(e->type,"DeploysInto").empty();
            }
        hud_state_.status=status_.text(clock_.now_ms());hud_state_.result=result_name(victory_.result);
        hud_state_.choices.clear();
        const auto eligible=proto::options(world_,factories_,catalog_,player_);
        auto available=sim::tech::buildable_types(rules_,world_,player_);
        // Keep the active product accessible when a prerequisite disappears.
        for(const auto& factory:factories_.factories)if(factory.house_id==player_&&factory.object) {
            const auto* type=catalog_.find(factory.object_type);if(!type)continue;
            auto& list=type->rtti==p::Rtti::BuildingType?available.buildings:
                type->rtti==p::Rtti::InfantryType?available.infantry:
                type->rtti==p::Rtti::AircraftType?available.aircraft:available.units;
            if(std::find(list.begin(),list.end(),type->id)==list.end())list.push_back(type->id);
        }
        // Keep the eligible types in their original rules order within each list.
        for(const auto* list:{&available.buildings,&available.units,&available.infantry,&available.aircraft})
        for(const auto& type:*list) {
            const auto* facts=catalog_.find(type);if(!facts)continue;
            const auto gate=std::find_if(eligible.begin(),eligible.end(),[&](const auto& item){return item.first==type;});
            const p::FactoryState* factory=nullptr;
            for(const auto& f:factories_.factories)if(f.house_id==player_&&f.object&&f.object_type==type)factory=&f;
            if((gate==eligible.end()||gate->second!=p::CanBuild::Can)&&!factory)continue;
            BuildChoice choice;choice.type=type;choice.name=name(type);choice.cost=facts->cost;choice.enabled=true;
            choice.category=facts->rtti==p::Rtti::BuildingType?BuildCategory::Buildings:
                facts->rtti==p::Rtti::InfantryType?BuildCategory::Infantry:
                facts->rtti==p::Rtti::AircraftType?BuildCategory::Aircraft:BuildCategory::Vehicles;
            if(factory) {
                choice.progress=factory->progress*1000/54;
                choice.held=factory->suspended&&factory->progress<54;
            }
            choice.queued=build_queue_.count(type)+active_count(type);
            hud_state_.choices.push_back(std::move(choice));
        }
        hud_state_.choices=sidebar_choices_.update(hud_state_.choices);
        hud_state_.camera={camera_x_,camera_y_,BattleHud::sidebar_x,BattleHud::view_height};
        hud_state_.map_width=terrain_.width;hud_state_.map_height=terrain_.height;
        // Base radar requires a live radar building and enough power.
        // TODO-ORIGINAL: Original shroud and house palettes remain separate.
        const auto bounds=BattleHud::minimap_bounds();
        auto& radar=hud_state_.minimap;
        if(sim::tech::radar_online(rules_,world_,player_)) {
            render::RadarView radar_view{geometry_,bounds.width,bounds.height,
                {camera_x_/60+geometry_.local.x,camera_y_/30+geometry_.local.y,
                 (BattleHud::sidebar_x+59)/60,(BattleHud::view_height+29)/30}};
            radar=render::draw_battle_radar(world_,assets_.theater(),radar_view,radar_style_,true);
            return;
        }
        radar={};
        hud_state_.map_width=hud_state_.map_height=0;
    }
    void present(const loop::State& state) override {
        // Headless checks compose at fixed intervals and at the final frame.
        if(args_.headless && presented && state.frame_counter%60 && !victory_.settled()&&!help_visible_&&!force_draw_)return;
        auto scene=[&] {
            if(initial_scene_ && state.frame_counter==0 && camera_x_==initial_camera_x_ && camera_y_==initial_camera_y_)
                return std::move(*initial_scene_);
            const auto drawing=[&] {
                loop::profile::Timer probe(loop::profile::Phase::SceneState);
                return battle_scene_state(world_,state.frame_counter);
            }();
            return scene_.render(terrain_,drawing,{camera_x_,camera_y_,1024,768},&world_);
        }();
        initial_scene_.reset();
        for(const auto& detail:scene.gaps)gaps_.insert(detail);
        frame_=std::move(scene.image);
        const w::Entity* hovered=nullptr;int nearest=24*24;
        if(!help_visible_&&pause_panel_==PausePanel::Closed&&!show_score_&&pointer_x_>=0&&pointer_x_<BattleHud::sidebar_x&&
            pointer_y_>=0&&pointer_y_<BattleHud::view_height)
            for(const auto& e:world_.entities)if(const auto at=anchor(e)) {
                const int dx=at->x-pointer_x_,dy=at->y-pointer_y_,d=dx*dx+dy*dy;
                if(d<nearest){nearest=d;hovered=&e;}
            }
        for(const auto& e:world_.entities)if(live(e)&&(e.house_id==player_||e.house_id==enemy_)) {
            const auto point=anchor(e);if(!point||point->x<0||point->x>=BattleHud::sidebar_x)continue;
            const auto color=radar_style_.houses.at(e.house_id);
            // TODO-ORIGINAL: House markers and health bars use simple native art.
            rectangle(frame_,point->x-5,point->y+9,10,3,color);
            const bool selected=std::find(orders_.selected_ids().begin(),orders_.selected_ids().end(),e.id)!=orders_.selected_ids().end();
            if(selected)rectangle(frame_,point->x-17,point->y-25,34,32,color);
            if(selected || hovered==&e) {
                const int health=std::clamp(e.resolved_hp.value_or(0)*30/std::max(1,e.maximum_hp.value_or(1)),0,30);
                for(int x=0;x<health;++x)for(int y=0;y<3;++y)pixel(frame_,point->x-15+x,point->y-30+y,color);
            }
        }
        effects_.draw(frame_,camera_x_,camera_y_,state.frame_counter);
        if(const auto box=orders_.selection_rectangle())rectangle(frame_,box->x,box->y,box->width,box->height,{70,255,90});
        bool placement_legal=false;
        if(!placement_.empty())if(const auto cell=cell_at({pointer_x_,pointer_y_})) {
            const auto cursor=proto::placement_cursor(world_,factories_,catalog_,player_,placement_,
                {std::int16_t(cell->x),std::int16_t(cell->y)});
            if(cursor) {
                placement_legal=cursor->legal==true;
                for(const auto& entry:cursor->cells) {
                    const int x=entry.cell.x,y=entry.cell.y;
                    if(x<0||y<0||x>=512||y>=512)continue;
                    const std::array<std::uint8_t,3> color=entry.legal_cell==true?
                        std::array<std::uint8_t,3>{70,255,90}:std::array<std::uint8_t,3>{255,70,70};
                    const auto found=world_.cells.find(std::uint32_t(y)*512+x);
                    const int z=found!=world_.cells.end()&&found->second.terrain?
                        found->second.terrain->floor_world_height.value_or(0):0;
                    w::Entity marker;marker.cell=ra2map::Cell{std::uint16_t(x),std::uint16_t(y)};
                    marker.position=sim::Coord{x*256+128,y*256+128,z};
                    marker.resolved_hp=1;marker.lifetime=w::Lifetime::Placed;
                    if(const auto at=anchor(marker))for(int dx=-30;dx<=30;++dx) {
                        const int dy=(30-std::abs(dx))/2;
                        pixel(frame_,at->x+dx,at->y-dy,color);
                        pixel(frame_,at->x+dx,at->y+dy,color);
                    }
                }
            }
        }
        refresh_hud();hud_.draw(frame_,hud_state_);
        if(show_score_ && score_image_) {
            const auto& score=*score_image_;
            for(int y=0;y<frame_.height;++y)for(int x=0;x<frame_.width;++x) {
                const auto source=(std::size_t(y*score.height/frame_.height)*score.width+x*score.width/frame_.width)*4;
                const auto target=(std::size_t(y)*frame_.width+x)*4;
                std::copy_n(score.pixels.begin()+source,4,frame_.rgba.begin()+target);
            }
        }
        if(help_visible_)help_.draw(frame_);
        if(pause_panel_!=PausePanel::Closed)help_.draw_pause(frame_,pause_panel_==PausePanel::Abort);
        CursorKind kind=CursorKind::Normal;
        if(!help_visible_&&pause_panel_==PausePanel::Closed&&!show_score_&&pointer_x_>=0&&pointer_x_<BattleHud::sidebar_x) {
            const auto cell=cell_at({pointer_x_,pointer_y_});
            bool passable=false;
            if(cell) {
                const auto at=world_.cells.find(std::uint32_t(cell->y)*512+cell->x);
                passable=at!=world_.cells.end()&&at->second.unit_land_speed.value_or(0)>0&&
                    at->second.cell_flag_100!=true;
            }
            if(!placement_.empty())kind=placement_legal?CursorKind::Deploy:CursorKind::NoGo;
            else if(hovered && hovered->house_id==player_)kind=
                rules_.read_string(hovered->type,"DeploysInto").empty()?CursorKind::Select:CursorKind::Deploy;
            else if(!orders_.selected_ids().empty()) {
                if(hovered && hovered->house_id==enemy_) {
                    bool can=false;
                    for(const auto id:orders_.selected_ids())can|=combat_.can_attack(world_,id,hovered->id);
                    kind=can?CursorKind::Attack:CursorKind::NoGo;
                } else kind=passable?CursorKind::Move:CursorKind::NoGo;
            }
        }
        if(pointer_x_>=0&&pointer_y_>=0)cursor_.draw(frame_,kind,pointer_x_,pointer_y_,state.frame_counter);
        window_.present(frame_.rgba.data(),frame_.width,frame_.height,frame_.rgba.size());++presented;
    }
    void verify(const loop::State& state) {
        // Force a final composition, including a settled result.
        presented=0;present(state);
        int width=0,height=0;const auto actual=window_.capture(width,height);
        if(width!=frame_.width||height!=frame_.height||actual!=frame_.rgba)
            throw std::runtime_error("Native match output differs from its composed frame");
        if(!w::validate(world_))throw std::runtime_error("Final match links are invalid");
        if(const auto* capture=std::getenv("RA2_MATCH_CAPTURE"))render::write_ppm(frame_,capture);
    }
    void report(std::uint32_t frame) const {
        std::cout<<"Native prototype skirmish: frame="<<frame<<" moved="<<moved<<" fired="<<fired
            <<" impacts="<<impacts<<" production="<<built<<" deployed="<<deployed<<" infantry="<<infantry
            <<" ai_orders="<<ai_orders<<" result="<<(victory_.settled()?result_name(victory_.result):"IN_PROGRESS")
            <<" world=PrototypeComplete\n";
        for(const auto& gap:gaps_)std::cout<<"TODO-ORIGINAL: "<<gap<<'\n';
        if((args_.autoplay||args_.human_input_check) && !victory_.settled())for(const auto& e:world_.entities)
            if(live(e)&&(e.house_id==player_||e.house_id==enemy_))
                std::cout<<"Survivor house="<<e.house_id<<" id="<<e.id<<" type="<<e.type<<" hp="
                    <<e.resolved_hp.value_or(0)<<" cell="<<e.cell->x<<','<<e.cell->y
                    <<" moving="<<movement_.moving(e.id)<<" attack="
                    <<(attacks_.count(e.id)?attacks_.at(e.id):0)<<'\n';
        if(ai_opening_check_) {
            if(!first_ai_shot_)throw std::runtime_error("AI opening check found no enemy shot");
            return;
        }
        if(args_.human_input_check && human_stage_<13)
            throw std::runtime_error("Human input check did not reach result and menu; stage="+std::to_string(human_stage_));
        if(args_.autoplay && (!autoplay_deployed_||!autoplay_moved_||!moved||!fired||!impacts||!built||!infantry||
            !ai_orders||!victory_.settled()))throw std::runtime_error("Autoplay did not complete the required match loop");
    }
};
}

int run_prototype_battle(const BattleOptions& requested) {
    auto args=requested;
    // A stationary normal opening for timing checks; normal matches use saved setup.
    if(const char* probe=std::getenv("RA2_AI_OPENING_CHECK")) {
        const std::string level=probe;
        if(level!="easy" && level!="medium" && level!="brutal")throw std::runtime_error("Unknown opening difficulty");
        args.setup.country="Americans";args.setup.unit_count=10;
        for(auto& slot:args.setup.opponents)slot.opponent=ui::Opponent::Closed;
        args.setup.opponents[0].opponent=level=="easy"?ui::Opponent::Easy:level=="medium"?ui::Opponent::Medium:ui::Opponent::Hard;
        if(const auto* speed=std::getenv("RA2_AI_OPENING_SPEED"))args.setup.game_speed=std::stoi(speed);
        if(!args.headless || !args.autoplay)throw std::runtime_error("Opening check needs headless autoplay");
    }
    auto assets=w::BaseBattleAssets::load(args.game_directory,args.map,args.setup.map_archive.value_or(""));
    // Merge the rules while the world is set up. Both only read the loaded
    // rules; overlay_rules keeps the result for the Battle's own request.
    auto rules_job=std::async(std::launch::async,[&]{
        return overlay_rules(overlay_rules(assets.rules(),assets.battle_rules()),assets.map_ini());
    });
    w::PrototypeBattleSetup setup;
    const auto* country_registry=assets.rules().find_section("Countries");
    if(!country_registry||country_registry->entries.empty())throw std::runtime_error("Base countries are missing");
    auto country=args.setup.country.value_or("Americans");
    if(!assets.rules().find_section(country)||assets.rules().read_string(country,"Side").empty())country="Americans";
    const int credits=std::clamp(args.setup.credits.value_or(10000),2500,10000);
    const int count=std::clamp(args.setup.unit_count.value_or(2),0,24);
    const int color=std::clamp(args.setup.color.value_or(0),0,7);
    const auto opponent=first_opponent_choice(args.setup,assets.rules(),country,color);
    // TODO-ORIGINAL: This first match supports one human and one computer,
    // Battle rules, two deterministic starting waypoints, and base factions.
    // Opponent slots after the first open one are not used.
    setup.players.push_back({"Player",country,credits,color,-1,count,true});
    setup.players.push_back({"Computer",opponent.country,credits,opponent.color,-1,count,false});
    const auto seed=sim::Random(0x524132).state();
    auto battle=w::prototype_battle(assets,setup,seed);
    if(!battle)throw std::runtime_error("Cannot create prototype Battle setup for "+args.map);
    for(const auto& gap:battle->gaps)std::cout<<"TODO-ORIGINAL: "<<gap<<'\n';
    sim::TerrainCells cells;
    for(const auto& [key,cell]:battle->snapshot.cells){(void)key;if(cell.terrain)cells.push_back(*cell.terrain);}
    const auto geometry=render::MapGeometry::from_ini(assets.map_ini());
    const auto terrain=allocated_terrain(pack::load_terrain(assets.map_ini()),geometry);
    // TODO-ORIGINAL: The prototype has translated tiles and approximate
    // floor metadata. Keep the exact initialized_terrain guard unchanged.
    auto initialized=terrain;
    for(auto& tile:initialized.tiles) {
        const auto found=battle->snapshot.cells.find(std::uint32_t(tile.y)*512+tile.x);
        if(found==battle->snapshot.cells.end()||!found->second.terrain)throw std::runtime_error("Prototype terrain cell is missing");
        const auto& cell=*found->second.terrain;
        tile.index=cell.resolved_tile_index.value_or(0xffff);
        tile.subtile=cell.subtile;tile.height=std::uint8_t(cell.height_level.value_or(0));
    }
    // Build the terrain image while the second file system, rules and scene
    // load. Nothing else uses the battle theater art until the match starts.
    auto terrain_job=std::async(std::launch::async,[&]{
        return render::render_minimap(initialized,geometry,assets.theater(),render::TileIndexSpace::LiveCell);
    });
    fs::MixFileSystem files(args.game_directory);
    if(args.setup.map_archive && !args.setup.map_archive->empty())files.mount(*args.setup.map_archive);
    files.mount_language(fs::GameVariant::RedAlert2);
    files.bootstrap(fs::GameVariant::RedAlert2);files.mount_overlays();files.mount_resources();
    auto scene_art=render::TheaterArt::load(files,assets.map_ini().read_string("Map","Theater"));
    (void)scene_art;
    const auto rules=rules_job.get();
    Scene scene(files,assets.art_ini(),rules,geometry,cells,assets.map_ini().read_string("Map","Theater"),
        assets.map().terrain(),true);
    std::vector<std::string> products;
    const auto catalog=p::Catalog::from(rules,{},{});
    for(const auto* list:{&catalog.buildings,&catalog.units,&catalog.infantry,&catalog.aircraft})
        for(const auto& type:*list)products.push_back(type.id);
    for(const auto& h:battle->snapshot.houses)
        if(h.selected_color && *h.selected_color>=0 && *h.selected_color<8)
            scene.set_house_color(h.name,*h.selected_color);
    scene.prepare(battle_scene_state(battle->snapshot,0),products);
    Window window("Red Alert 2 native prototype: skirmish",1024,768,!args.headless);
    loop::SteadyClock real_clock;loop::SimulatedClock fast_clock;
    loop::Clock& clock=args.headless?static_cast<loop::Clock&>(fast_clock):static_cast<loop::Clock&>(real_clock);
    const auto terrain_image=terrain_job.get();
    Battle match(args,assets,std::move(battle->snapshot),window,scene,terrain_image,clock,files);
    loop::Hooks hooks;hooks.update=[&](loop::State& state){match.update(state);};
    loop::Config config;
    // Base 0053FBD0 copies the saved speed directly to the 16 ms tick delay.
    config.local_delay_ticks=frame_delay_ticks(args);
    loop::GameLoop loop(match,match,clock,config,hooks);
    if(args.frames)loop.run(args.frames);else while(loop.state().running)loop.step();
    if(args.headless)match.verify(loop.state());
    match.report(loop.state().frame_counter);
    return 0;
}
}
