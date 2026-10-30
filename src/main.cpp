#include <Geode/Geode.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/GameManager.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <filesystem>
#include <vector>
#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#endif

using namespace geode::prelude;
using Clock = std::chrono::steady_clock;

// ---------- settings ----------
struct Cfg {
    bool speedOn = false;  double speed = 1.0;
    bool fpsOn = false;    int fps = 240;
    bool noclip = false;   int ncMax = 3;  bool ncUnl = false;
    bool hitbox = false;
    bool cps = false;
    bool autoRetry = false;
    bool iconHack = false;
    bool rgb = false;      int rgbMode = 3;  // 1 color 1, 2 color 2, 3 both
    bool instRespawn = false;
    bool trail = false;    int trailLen = 40;
    bool sync = false;
    int accent = 0;
};
static Cfg C;
static int g_saves = 0, g_total = 0, g_best = 0;
static std::vector<int> g_deaths;
static std::deque<Clock::time_point> g_clicks;

template <class T>
static void io1(bool load, char const* k, T& v) {
    auto m = Mod::get();
    if (load) v = m->getSavedValue<T>(k, v); else m->setSavedValue<T>(k, v);
}
static void io(bool l) {
    io1(l, "speedOn", C.speedOn); io1(l, "speed", C.speed);
    io1(l, "fpsOn", C.fpsOn);     io1(l, "fps", C.fps);
    io1(l, "noclip", C.noclip);   io1(l, "ncMax", C.ncMax); io1(l, "ncUnl", C.ncUnl);
    io1(l, "hitbox", C.hitbox);   io1(l, "cps", C.cps);
    io1(l, "autoRetry", C.autoRetry); io1(l, "iconHack", C.iconHack);
    io1(l, "rgb", C.rgb); io1(l, "rgbMode", C.rgbMode); io1(l, "sync", C.sync); io1(l, "accent", C.accent);
    io1(l, "instRespawn", C.instRespawn); io1(l, "trail", C.trail); io1(l, "trailLen", C.trailLen);
    if (C.rgbMode < 1 || C.rgbMode > 3) C.rgbMode = 3;
}
$on_mod(Loaded) { io(true); }

static ccColor3B const PAL[6] = {{56,189,248},{34,197,94},{244,63,94},{250,204,21},{168,85,247},{249,115,22}};

static ccColor3B hsv(float h) {
    float r, g, b; int i = int(h * 6); float f = h * 6 - i, q = 1 - f;
    switch (i % 6) {
        case 0: r=1; g=f; b=0; break; case 1: r=q; g=1; b=0; break;
        case 2: r=0; g=1; b=f; break; case 3: r=0; g=q; b=1; break;
        case 4: r=f; g=0; b=1; break; default: r=1; g=0; b=q;
    }
    return {(GLubyte)(r*255), (GLubyte)(g*255), (GLubyte)(b*255)};
}

static std::vector<Ref<StartPosObject>> g_starts;
static int g_startIdx = -1;
static PlayLayer* g_owner = nullptr;
static void switchStart(int d) {
    auto pl = PlayLayer::get();
    if (!pl || g_starts.empty()) return;
    std::sort(g_starts.begin(), g_starts.end(), [](auto& a, auto& b) { return a->getPositionX() < b->getPositionX(); });
    g_startIdx = std::clamp(g_startIdx + d, -1, (int)g_starts.size() - 1);
    pl->setStartPosObject(g_startIdx < 0 ? nullptr : g_starts[g_startIdx].data());
    pl->resetLevel();
}

static std::vector<std::filesystem::path> songs() {
    std::vector<std::filesystem::path> v;
    std::error_code ec;
    auto d = Mod::get()->getSaveDir() / "music";
    std::filesystem::create_directories(d, ec);
    for (auto& e : std::filesystem::directory_iterator(d, ec)) {
        auto x = e.path().extension().string();
        if (x == ".mp3" || x == ".ogg" || x == ".wav") v.push_back(e.path());
    }
    return v;
}

// ---------- in-game menu ----------
class VMenu : public CCLayer {
public:
    static inline VMenu* inst = nullptr;
    int m_tab = 0, m_sub = -1;
    CCNode* m_page = nullptr;

    static void toggle() {
        if (inst) { inst->removeFromParentAndCleanup(true); return; }
        auto s = CCDirector::get()->getRunningScene();
        if (!s) return;
        auto m = new VMenu();
        if (m && m->init()) { m->autorelease(); s->addChild(m, 99999); }
        else { delete m; }
    }
    ~VMenu() { if (inst == this) inst = nullptr; }

    bool init() override {
        if (!CCLayer::init()) return false;
        inst = this;
        auto ws = CCDirector::get()->getWinSize();
        this->addChild(CCLayerColor::create({0, 0, 0, 150}));
        auto bg = cocos2d::extension::CCScale9Sprite::create("GJ_square01.png", {0, 0, 80, 80});
        bg->setContentSize({430, 270});
        bg->setPosition(ccp(ws.width / 2, ws.height / 2));
        this->addChild(bg);
        this->setTouchEnabled(true);
        showPage();
        return true;
    }
    void registerWithTouchDispatcher() override {
        CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, -1000, true);
    }
    bool ccTouchBegan(CCTouch*, CCEvent*) override { return true; }

    void after() {
        io(false);
        GameManager::get()->setGameVariable("0026", C.autoRetry);
        showPage();
    }
    void onTab(CCObject* s) { m_tab = static_cast<CCNode*>(s)->getTag(); m_sub = -1; showPage(); }
    void onCog(CCObject* s) { m_sub = static_cast<CCNode*>(s)->getTag(); showPage(); }
    void onBack(CCObject*) { m_sub = -1; showPage(); }
    void onClose(CCObject*) { this->removeFromParentAndCleanup(true); }
    void onAccent(CCObject* s) { C.accent = static_cast<CCNode*>(s)->getTag(); after(); }
    void onSong(CCObject* s) {
        auto v = songs(); size_t i = static_cast<CCNode*>(s)->getTag();
        if (i < v.size()) FMODAudioEngine::sharedEngine()->playMusic(v[i].string(), false, 0.f, 0);
    }
    void onToggle(CCObject* s) {
        auto t = static_cast<CCMenuItemToggler*>(s);
        bool v = !t->isToggled();  // state before the click is what isToggled() reports
        switch (t->getTag()) {
            case 0: C.speedOn = v; break;  case 1: C.fpsOn = v; break;
            case 2: C.noclip = v; break;   case 3: C.ncUnl = v; break;
            case 4: C.autoRetry = v; break; case 5: C.hitbox = v; break;
            case 6: C.iconHack = v; break; case 7: C.cps = v; break;
            case 8: C.sync = v; break;     case 9: C.instRespawn = v; break;
            case 10: C.trail = v; break;   case 11: C.rgb = v; break;
        }
        after();
    }
    void onStep(CCObject* s) {
        int tg = static_cast<CCNode*>(s)->getTag(), id = tg / 2, d = (tg % 2) ? 1 : -1;
        switch (id) {
            case 0: C.speed = std::round(std::clamp(C.speed + d * 0.1, 0.1, 10.0) * 10) / 10; break;
            case 1: C.fps = std::clamp(C.fps + d * 30, 30, 999); break;
            case 2: C.ncMax = std::clamp(C.ncMax + d, 0, 99); break;
            case 3: C.rgbMode = (C.rgbMode - 1 + d + 3) % 3 + 1; break;
            case 4: C.trailLen = std::clamp(C.trailLen + d * 10, 10, 200); break;
            case 5: switchStart(d); break;
        }
        after();
    }

    void showPage() {
        if (m_page) m_page->removeFromParentAndCleanup(true);
        m_page = CCNode::create();
        this->addChild(m_page, 5);
        auto menu = CCMenu::create();
        menu->setPosition({0, 0});
        menu->setTouchPriority(-1001);
        m_page->addChild(menu);
        auto ws = CCDirector::get()->getWinSize();
        float cx = ws.width / 2, cy = ws.height / 2;
        auto acc = PAL[C.accent % 6];
        ccColor3B white = {255, 255, 255};

        auto lbl = [&](std::string t, float x, float y, float sc, ccColor3B c, bool left) {
            auto l = CCLabelBMFont::create(t.c_str(), "bigFont.fnt");
            l->setScale(sc); l->setPosition(ccp(x, y)); l->setColor(c);
            if (left) l->setAnchorPoint({0, .5f});
            m_page->addChild(l); return l;
        };
        auto btn = [&](char const* s, int tag, SEL_MenuHandler cb, float x, float y, float sc) {
            auto spr = ButtonSprite::create(s); spr->setScale(sc);
            auto b = CCMenuItemSpriteExtra::create(spr, nullptr, this, cb);
            b->setTag(tag); b->setPosition(ccp(x, y)); menu->addChild(b);
        };
        auto tog = [&](int tag, bool on, float x, float y) {
            auto t = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(VMenu::onToggle), .6f);
            t->toggle(on); t->setTag(tag); t->setPosition(ccp(x, y)); menu->addChild(t);
        };
        auto cog = [&](int id, float y) {
            auto spr = CCSprite::createWithSpriteFrameName("GJ_optionsBtn02_001.png");
            spr->setScale(.55f);
            auto b = CCMenuItemSpriteExtra::create(spr, nullptr, this, menu_selector(VMenu::onCog));
            b->setTag(id); b->setPosition(ccp(cx + 70, y)); menu->addChild(b);
        };
        auto step = [&](int id, std::string v, float y) {
            btn("-", id * 2, menu_selector(VMenu::onStep), cx + 50, y, .5f);
            lbl(v, cx + 120, y, .4f, white, false);
            btn("+", id * 2 + 1, menu_selector(VMenu::onStep), cx + 190, y, .5f);
        };
        auto row = [&](int i, char const* name) {
            float y = cy + 50 - 36 * i;
            lbl(name, cx - 195, y, .45f, white, true);
            return y;
        };
        char const* modes[4] = {"", "Color 1", "Color 2", "Both"};

        lbl("VELOCITY", cx, cy + 118, .8f, acc, false);
        if (m_sub >= 0) {
            char const* names[6] = {"Speedhack", "FPS bypass", "No Clip+", "Show hitboxes", "RGB cube", ""};
            lbl(names[m_sub], cx, cy + 82, .55f, white, false);
            if (m_sub == 0) { float y = row(0, "Speed"); step(0, fmt::format("{:.1f}x", C.speed), y); }
            if (m_sub == 1) { float y = row(0, "FPS"); step(1, std::to_string(C.fps), y); }
            if (m_sub == 2) {
                float y = row(0, "Max deaths"); step(2, C.ncUnl ? "inf" : std::to_string(C.ncMax), y);
                y = row(1, "Unlimited"); tog(3, C.ncUnl, cx + 120, y);
            }
            if (m_sub == 3) {
                float y = row(0, "Trail"); tog(10, C.trail, cx + 120, y);
                y = row(1, "Trail length"); step(4, std::to_string(C.trailLen), y);
            }
            if (m_sub == 4) { float y = row(0, "Colours"); step(3, modes[C.rgbMode], y); }
            btn("Back", 0, menu_selector(VMenu::onBack), cx, cy - 118, .5f);
            return;
        }

        char const* tabs[6] = {"Play", "Look", "Practice", "Stats", "Music", "Style"};
        for (int i = 0; i < 6; i++) btn(tabs[i], i, menu_selector(VMenu::onTab), cx - 165 + i * 66, cy + 88, i == m_tab ? .4f : .32f);
        btn("Close", 0, menu_selector(VMenu::onClose), cx, cy - 118, .5f);

        if (m_tab == 0) {
            float y = row(0, "Speedhack"); tog(0, C.speedOn, cx + 20, y); cog(0, y);
            y = row(1, "FPS bypass"); tog(1, C.fpsOn, cx + 20, y); cog(1, y);
            y = row(2, "No Clip+"); tog(2, C.noclip, cx + 20, y); cog(2, y);
            y = row(3, "Auto retry"); tog(4, C.autoRetry, cx + 20, y);
            y = row(4, "Instant respawn"); tog(9, C.instRespawn, cx + 20, y);
        } else if (m_tab == 1) {
            float y = row(0, "Show hitboxes"); tog(5, C.hitbox, cx + 20, y); cog(3, y);
            y = row(1, "RGB cube"); tog(11, C.rgb, cx + 20, y); cog(4, y);
            y = row(2, "Icon hack"); tog(6, C.iconHack, cx + 20, y);
            y = row(3, "Click counter"); tog(7, C.cps, cx + 20, y);
        } else if (m_tab == 2) {
            float y = row(0, "Start position");
            std::string cur = g_startIdx < 0 ? "Level start" : fmt::format("Start {}/{}", g_startIdx + 1, g_starts.size());
            if (!PlayLayer::get()) cur = "Open a level";
            step(5, cur, y);
            lbl("PC keys: Q previous, E next", cx - 195, y - 30, .33f, acc, true);
            y = row(2, "Sync music to speed"); tog(8, C.sync, cx + 20, y);
        } else if (m_tab == 3) {
            lbl(fmt::format("Best CPS: {}", g_best), cx - 195, cy + 50, .45f, white, true);
            lbl(fmt::format("Total clicks: {}", g_total), cx - 195, cy + 14, .45f, white, true);
            lbl(fmt::format("No Clip saves: {}", g_saves), cx - 195, cy - 22, .45f, white, true);
            std::string d = "Saved at: ";
            for (auto p : g_deaths) d += std::to_string(p) + "% ";
            lbl(g_deaths.empty() ? "Saved at: none yet" : d, cx - 195, cy - 58, .35f, acc, true);
        } else if (m_tab == 4) {
            auto v = songs();
            if (v.empty()) lbl("Put mp3/ogg files in the VELOCITY music folder", cx - 195, cy + 30, .33f, acc, true);
            for (size_t i = 0; i < v.size() && i < 5; i++) {
                auto n = v[i].stem().string().substr(0, 28);
                btn(n.c_str(), (int)i, menu_selector(VMenu::onSong), cx, cy + 50 - 34 * i, .42f);
            }
        } else {
            lbl("Accent colour", cx - 195, cy + 50, .45f, white, true);
            for (int i = 0; i < 6; i++) {
                auto sq = CCSprite::create("whiteSquare60_001.png");
                sq->setColor(PAL[i]); sq->setScale(.5f);
                auto b = CCMenuItemSpriteExtra::create(sq, nullptr, this, menu_selector(VMenu::onAccent));
                b->setTag(i); b->setPosition(ccp(cx - 130 + i * 50, cy + 10)); menu->addChild(b);
            }
        }
    }
};

// ---------- speedhack, FPS bypass, menu key, music sync ----------
static int64_t g_lastFps = 0;
static bool g_wasSync = false;
#ifdef GEODE_IS_WINDOWS
static bool g_tabWas = false, g_qWas = false, g_eWas = false;
#endif

class $modify(VScheduler, CCScheduler) {
    void update(float dt) {
#ifdef GEODE_IS_WINDOWS
        bool tab = GetActiveWindow() != nullptr && (GetAsyncKeyState(VK_TAB) & 0x8000);
        if (tab && !g_tabWas) VMenu::toggle();
        g_tabWas = tab;
        bool q = GetActiveWindow() != nullptr && (GetAsyncKeyState('Q') & 0x8000);
        bool e = GetActiveWindow() != nullptr && (GetAsyncKeyState('E') & 0x8000);
        if (q && !g_qWas) switchStart(-1);
        if (e && !g_eWas) switchStart(1);
        g_qWas = q; g_eWas = e;
#endif
        if (C.fpsOn) {
            if (C.fps != g_lastFps) {
                g_lastFps = C.fps;
                CCDirector::sharedDirector()->setAnimationInterval(1.0 / C.fps);
            }
        } else if (g_lastFps != 0) {
            g_lastFps = 0;
            CCDirector::sharedDirector()->setAnimationInterval(1.0 / 60.0);
        }

        if (C.sync || g_wasSync) {
            auto ch = FMODAudioEngine::sharedEngine()->m_backgroundMusicChannel;
            if (ch) ch->setPitch(C.sync && C.speedOn ? (float)C.speed : 1.f);
            g_wasSync = C.sync;
        }

        if (C.speedOn) dt *= (float)C.speed;
        CCScheduler::update(dt);
    }
};

// ---------- icon hack ----------
class $modify(VGameManager, GameManager) {
    bool isIconUnlocked(int id, IconType type) {
        return C.iconHack ? true : GameManager::isIconUnlocked(id, type);
    }
    bool isColorUnlocked(int id, UnlockType type) {
        return C.iconHack ? true : GameManager::isColorUnlocked(id, type);
    }
};

// ---------- click counter ----------
class $modify(VGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        if (down) { g_clicks.push_back(Clock::now()); g_total++; }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};

// ---------- No Clip+, hitboxes, RGB cube, overlay, start positions ----------
class $modify(VPlayLayer, PlayLayer) {
    struct Fields {
        int hits = 0;
        float hue = 0.f;
        CCLabelBMFont* lbl = nullptr;
        CCDrawNode* trail = nullptr;
        std::deque<CCRect> rects;
    };

    void addObject(GameObject* obj) {
        if (g_owner != this) { g_owner = this; g_starts.clear(); g_startIdx = -1; }
        if (obj->m_objectID == 31) g_starts.push_back(static_cast<StartPosObject*>(obj));
        PlayLayer::addObject(obj);
    }

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        auto l = CCLabelBMFont::create("", "bigFont.fnt");
        l->setScale(.4f); l->setOpacity(170); l->setAnchorPoint({0, 1});
        l->setPosition(ccp(8, CCDirector::get()->getWinSize().height - 8));
        m_uiLayer->addChild(l, 1000);
        m_fields->lbl = l;
        auto t = CCDrawNode::create();
        m_objectLayer->addChild(t, 1000);
        m_fields->trail = t;
    }

    void resetLevel() {
        m_fields->hits = 0;
        m_fields->rects.clear();
        PlayLayer::resetLevel();
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        bool real = object == m_anticheatSpike;
        if (C.noclip && !real) {
            m_fields->hits++;
            if (C.ncUnl || m_fields->hits <= C.ncMax) {
                g_saves++;
                g_deaths.push_back((int)this->getCurrentPercent());
                if (g_deaths.size() > 8) g_deaths.erase(g_deaths.begin());
                return;
            }
        }
        PlayLayer::destroyPlayer(player, object);
        if (C.instRespawn && !real && m_player1 && m_player1->m_isDead) {
            this->stopActionByTag(0x10);
            this->delayedResetLevel();
        }
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        if (C.hitbox) {
            m_isDebugDrawEnabled = true;
            if (m_debugDrawNode) m_debugDrawNode->setVisible(true);
            this->updateDebugDraw();
        }

        if (auto t = m_fields->trail) {
            t->clear();
            if (C.hitbox && C.trail && m_player1) {
                auto& rs = m_fields->rects;
                rs.push_back(m_player1->getObjectRect());
                while ((int)rs.size() > C.trailLen) rs.pop_front();
                int n = (int)rs.size();
                for (int i = 0; i < n; i++) {
                    auto& r = rs[i];
                    float x0 = r.origin.x, y0 = r.origin.y, x1 = x0 + r.size.width, y1 = y0 + r.size.height;
                    CCPoint pts[4] = {ccp(x0, y0), ccp(x1, y0), ccp(x1, y1), ccp(x0, y1)};
                    t->drawPolygon(pts, 4, ccc4f(0, 0, 0, 0), 1.f, ccc4f(1.f, .2f, .2f, (float)(i + 1) / n));
                }
            } else {
                m_fields->rects.clear();
            }
        }

        if (C.rgb) {
            m_fields->hue += dt * 0.5f;
            if (m_fields->hue > 1.f) m_fields->hue -= 1.f;
            auto c = hsv(m_fields->hue);
            for (auto p : {m_player1, m_player2}) {
                if (!p) continue;
                if (C.rgbMode & 1) p->setColor(c);
                if (C.rgbMode & 2) p->setSecondColor(c);
            }
        }

        if (m_fields->lbl) {
            m_fields->lbl->setVisible(C.cps);
            if (C.cps) {
                auto n = Clock::now();
                while (!g_clicks.empty() && n - g_clicks.front() > std::chrono::seconds(1)) g_clicks.pop_front();
                int cur = (int)g_clicks.size();
                g_best = std::max(g_best, cur);
                m_fields->lbl->setString(fmt::format("CPS {}  Best {}  Clicks {}", cur, g_best, g_total).c_str());
            }
        }
    }
};

// ---------- on-screen V button (Android, and PC if you like) ----------
class $modify(VMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        auto menu = CCMenu::create();
        auto spr = ButtonSprite::create("V"); spr->setScale(.6f);
        auto b = CCMenuItemSpriteExtra::create(spr, nullptr, this, menu_selector(VMenuLayer::onV));
        menu->addChild(b);
        menu->setPosition(ccp(30, CCDirector::get()->getWinSize().height - 30));
        this->addChild(menu, 100);
        return true;
    }
    void onV(CCObject*) { VMenu::toggle(); }
};

class $modify(VPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto menu = CCMenu::create();
        menu->setTouchPriority(-600);
        auto spr = ButtonSprite::create("V"); spr->setScale(.6f);
        auto b = CCMenuItemSpriteExtra::create(spr, nullptr, this, menu_selector(VPauseLayer::onV));
        menu->addChild(b);
        menu->setPosition(ccp(30, CCDirector::get()->getWinSize().height - 30));
        this->addChild(menu, 100);
    }
    void onV(CCObject*) { VMenu::toggle(); }
};
