// Ganchos (hooks) sobre el juego:
//   - MenuLayer:        arranca la conexion y el "latido" global del mod
//   - EditLevelLayer:   agrega el boton "Colab" en tus niveles creados
//   - LevelEditorLayer: sincroniza objetos y cursores en el editor

#include <Geode/Geode.hpp>
#include <Geode/modify/EditLevelLayer.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Collab.hpp"
#include "FriendsPopup.hpp"

using namespace geode::prelude;

// ---------------------------------------------------------------
// Latido global: llama a Collab::pump() 20 veces por segundo
// ---------------------------------------------------------------
class CollabPump : public CCNode {
public:
    static CollabPump* create() {
        auto ret = new CollabPump();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    void tick(float dt) { Collab::get().pump(dt); }
};

class $modify(FCMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        static bool started = false;
        if (!started) {
            started = true;
            Collab::get().start();
            if (auto pump = CollabPump::create()) {
                pump->retain();
                CCDirector::get()->getScheduler()->scheduleSelector(
                    schedule_selector(CollabPump::tick), pump, 0.05f, false
                );
            }
        }
        return true;
    }
};

// ---------------------------------------------------------------
// Boton "Colab" en la pantalla de tus niveles creados
// ---------------------------------------------------------------
class $modify(FCEditLevelLayer, EditLevelLayer) {
    bool init(GJGameLevel* level) {
        if (!EditLevelLayer::init(level)) return false;

        auto spr = ButtonSprite::create("Colab", "bigFont.fnt", "GJ_button_01.png", 0.7f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(FCEditLevelLayer::onCollab));
        btn->setID("collab-button"_spr);
        m_buttonMenu->addChild(btn);

        // abajo a la izquierda (cambia estos numeros si choca con otro boton)
        btn->setPosition(m_buttonMenu->convertToNodeSpace(ccp(60.f, 40.f)));
        return true;
    }

    void onCollab(CCObject*) {
        if (auto popup = FriendsPopup::create(m_level)) popup->show();
    }
};

// ---------------------------------------------------------------
// Editor colaborativo
// ---------------------------------------------------------------
namespace {
    // true mientras aplicamos un cambio ajeno (para no reenviarlo y hacer un bucle)
    bool g_applying = false;

    struct CursorView {
        Ref<CCNode> node;
        float tx = 0.f; // destino (coordenadas del nivel)
        float ty = 0.f;
        float cx = 0.f; // posicion suavizada actual
        float cy = 0.f;
        bool placed = false;
    };

    std::string trimEnd(std::string s) {
        while (!s.empty() && (s.back() == ';' || s.back() == '\n' || s.back() == '\r')) s.pop_back();
        return s;
    }

    ccColor3B colorFor(std::string const& name) {
        static const ccColor3B palette[] = {
            {255, 110, 110}, {110, 200, 255}, {120, 255, 140},
            {255, 210, 90},  {210, 140, 255}, {255, 150, 220},
        };
        return palette[std::hash<std::string>{}(name) % 6];
    }
}

class $modify(FCEditor, LevelEditorLayer) {
    struct Fields {
        bool active = false;
        int ticks = 0;
        unsigned knownCount = 0;
        unsigned nextId = 1;
        CCPoint lastMouse = ccp(-99999.f, -99999.f);

        // identificador compartido <-> objeto local
        std::unordered_map<std::string, Ref<GameObject>> byGid;
        // objeto local -> { identificador, ultimo texto enviado }
        std::unordered_map<GameObject*, std::pair<std::string, std::string>> info;

        std::unordered_map<std::string, CursorView> cursors;
    };

    // ----- entrada al editor -----
    bool init(GJGameLevel* level, bool noUI) {
        if (!LevelEditorLayer::init(level, noUI)) return false;

        std::vector<std::string> replay;
        if (Collab::get().attachEditor(level, replay)) {
            m_fields->active = true;

            // Los objetos del nivel base reciben identificadores b0, b1, b2...
            // en orden; todos cargan el mismo nivel, asi que coinciden.
            unsigned count = m_objects ? m_objects->count() : 0;
            for (unsigned i = 0; i < count; ++i) {
                auto obj = static_cast<GameObject*>(m_objects->objectAtIndex(i));
                this->fcTrack(obj, "b" + std::to_string(i), "");
            }

            // cambios que ya habia en la sala
            for (auto const& r : replay) this->fcApplyOp("servidor", r);
            m_fields->knownCount = m_objects ? m_objects->count() : 0;

            this->schedule(schedule_selector(FCEditor::collabTick), 0.05f);
        }
        return true;
    }

    // ----- seguimiento de objetos -----
    void fcTrack(GameObject* obj, std::string const& gid, std::string const& last) {
        m_fields->byGid[gid] = obj;
        m_fields->info[obj] = {gid, last};
    }

    void fcUntrack(GameObject* obj) {
        auto it = m_fields->info.find(obj);
        if (it == m_fields->info.end()) return;
        m_fields->byGid.erase(it->second.first);
        m_fields->info.erase(it);
    }

    std::string fcObjString(GameObject* obj) {
        std::string s = obj->getSaveString(this);
        return trimEnd(std::move(s));
    }

    // ----- borrar (hecho por mi) -----
    void removeObject(GameObject* obj, bool noUndo) {
        if (m_fields->active && !g_applying && obj) {
            auto it = m_fields->info.find(obj);
            if (it != m_fields->info.end()) {
                std::string gid = it->second.first;
                this->fcUntrack(obj);
                if (m_fields->knownCount > 0) m_fields->knownCount--;
                Collab::get().sendOp("DEL " + gid);
            }
        }
        LevelEditorLayer::removeObject(obj, noUndo);
    }

    // ----- latido del editor (20 veces por segundo) -----
    void collabTick(float) {
        auto& collab = Collab::get();
        if (!m_fields->active || !collab.inRoom()) return;
        m_fields->ticks++;

        // 1) cambios que mandaron los demas
        for (auto const& op : collab.takeOps()) this->fcApplyOp(op.from, op.payload);

        // 2) objetos nuevos que cree yo
        this->fcScanNew();

        // 3) objetos seleccionados que cambie (mover, rotar, escalar, propiedades, triggers...)
        this->fcScanSelected();

        // 4) mi cursor (10 veces por segundo)
        if (m_fields->ticks % 2 == 0) this->fcSendCursor();

        // 5) dibujar los cursores de los demas
        this->fcUpdateCursors();
    }

    void fcScanNew() {
        unsigned count = m_objects ? m_objects->count() : 0;
        if (count > m_fields->knownCount) {
            for (unsigned i = m_fields->knownCount; i < count; ++i) {
                auto obj = static_cast<GameObject*>(m_objects->objectAtIndex(i));
                if (!obj || m_fields->info.count(obj)) continue;

                std::string s = this->fcObjString(obj);
                std::string gid = "n" + Collab::get().myName() + "-" + std::to_string(m_fields->nextId++);
                this->fcTrack(obj, gid, s);
                Collab::get().sendOp("SET " + gid + " " + s);
            }
        }
        m_fields->knownCount = count;
    }

    void fcScanSelected() {
        if (!m_editorUI) return;

        std::vector<GameObject*> list;
        auto sel = m_editorUI->getSelectedObjects();
        if (sel && sel->count() > 0) {
            for (unsigned i = 0; i < sel->count(); ++i) {
                list.push_back(static_cast<GameObject*>(sel->objectAtIndex(i)));
            }
        } else if (m_editorUI->m_selectedObject) {
            list.push_back(m_editorUI->m_selectedObject);
        }

        // con selecciones enormes revisamos menos seguido para no trabar el editor
        if (list.size() > 400 && (m_fields->ticks % 4) != 0) return;

        for (auto obj : list) {
            auto it = m_fields->info.find(obj);
            if (it == m_fields->info.end()) continue;

            std::string s = this->fcObjString(obj);
            auto& last = it->second.second;
            if (last.empty()) { // primera vez que lo vemos: solo guardamos como referencia
                last = s;
                continue;
            }
            if (s != last) {
                last = s;
                Collab::get().sendOp("SET " + it->second.first + " " + s);
            }
        }
    }

    // ----- aplicar lo que hicieron los demas -----
    void fcApplyOp(std::string const& from, std::string const& payload) {
        auto sp = payload.find(' ');
        std::string kind = payload.substr(0, sp);
        std::string rest = sp == std::string::npos ? "" : payload.substr(sp + 1);

        if (kind == "CUR") {
            float x = 0.f, y = 0.f;
            if (std::sscanf(rest.c_str(), "%f %f", &x, &y) == 2) this->fcShowCursor(from, x, y);
        }
        else if (kind == "DEL") {
            this->fcRemoveRemote(rest);
        }
        else if (kind == "SET") {
            auto sp2 = rest.find(' ');
            if (sp2 == std::string::npos) return;
            this->fcSetRemote(rest.substr(0, sp2), rest.substr(sp2 + 1));
        }
    }

    void fcRemoveRemote(std::string const& gid) {
        auto it = m_fields->byGid.find(gid);
        if (it == m_fields->byGid.end()) return;

        GameObject* obj = it->second.data();
        this->fcUntrack(obj);

        g_applying = true;
        if (m_editorUI) m_editorUI->deselectObject(obj);
        LevelEditorLayer::removeObject(obj, true);
        g_applying = false;

        m_fields->knownCount = m_objects ? m_objects->count() : 0;
    }

    void fcSetRemote(std::string const& gid, std::string const& text) {
        bool wasSelected = false;

        // si ya existe, lo reemplazamos por la version nueva
        if (auto it = m_fields->byGid.find(gid); it != m_fields->byGid.end()) {
            GameObject* old = it->second.data();
            wasSelected = old->m_isSelected;
            this->fcUntrack(old);

            g_applying = true;
            if (m_editorUI) m_editorUI->deselectObject(old);
            LevelEditorLayer::removeObject(old, true);
            g_applying = false;
        }

        g_applying = true;
        auto created = this->createObjectsFromString(text + ";", true, true);
        g_applying = false;

        if (created && created->count() > 0) {
            auto obj = static_cast<GameObject*>(created->objectAtIndex(0));
            this->fcTrack(obj, gid, this->fcObjString(obj));
            if (wasSelected && m_editorUI) m_editorUI->selectObject(obj, true);
        }

        m_fields->knownCount = m_objects ? m_objects->count() : 0;
    }

    // ----- cursores -----
    void fcSendCursor() {
        auto mouse = geode::cocos::getMousePos();
        if (std::fabs(mouse.x - m_fields->lastMouse.x) + std::fabs(mouse.y - m_fields->lastMouse.y) < 1.f) return;
        m_fields->lastMouse = mouse;

        auto p = m_objectLayer->convertToNodeSpace(mouse);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "CUR %.1f %.1f", p.x, p.y);
        Collab::get().sendOp(buf);
    }

    void fcShowCursor(std::string const& name, float x, float y) {
        auto& view = m_fields->cursors[name];
        if (!view.node) {
            auto node = CCNode::create();
            auto color = colorFor(name);

            auto arrow = CCDrawNode::create();
            CCPoint tri[3] = {ccp(0.f, 0.f), ccp(0.f, -18.f), ccp(12.f, -13.f)};
            arrow->drawPolygon(
                tri, 3,
                ccc4f(color.r / 255.f, color.g / 255.f, color.b / 255.f, 1.f),
                1.f, ccc4f(0.f, 0.f, 0.f, 1.f)
            );
            node->addChild(arrow);

            auto label = CCLabelBMFont::create(name.c_str(), "bigFont.fnt");
            label->setScale(0.35f);
            label->setAnchorPoint(ccp(0.f, 1.f));
            label->setPosition(ccp(14.f, -14.f));
            label->setColor(color);
            node->addChild(label);

            // se agrega al editor (no a la capa de objetos) para no mezclarse con los bloques
            this->addChild(node, 9999);
            view.node = node;
        }
        view.tx = x;
        view.ty = y;
    }

    void fcUpdateCursors() {
        auto& collab = Collab::get();

        // quitar los cursores de quien ya no esta en la sala
        if (m_fields->ticks % 10 == 0) {
            auto const& members = collab.members();
            for (auto it = m_fields->cursors.begin(); it != m_fields->cursors.end();) {
                bool present = std::find(members.begin(), members.end(), it->first) != members.end();
                if (!present) {
                    if (it->second.node) it->second.node->removeFromParent();
                    it = m_fields->cursors.erase(it);
                } else {
                    ++it;
                }
            }
        }

        for (auto& [name, view] : m_fields->cursors) {
            if (!view.node) continue;

            // suavizado en coordenadas del nivel
            if (!view.placed) {
                view.cx = view.tx;
                view.cy = view.ty;
                view.placed = true;
            } else {
                view.cx += (view.tx - view.cx) * 0.5f;
                view.cy += (view.ty - view.cy) * 0.5f;
            }

            // del nivel a la pantalla: asi el cursor mantiene siempre el mismo tamano
            auto world = m_objectLayer->convertToWorldSpace(ccp(view.cx, view.cy));
            view.node->setPosition(this->convertToNodeSpace(world));
        }
    }
};
