#include "FriendsPopup.hpp"
#include "Collab.hpp"

#include <algorithm>

using namespace geode::prelude;

static constexpr int PER_PAGE = 5;

FriendsPopup* FriendsPopup::create(GJGameLevel* level) {
    auto ret = new FriendsPopup();
    if (ret->init(level)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool FriendsPopup::init(GJGameLevel* level) {
    if (!Popup::init(340.f, 270.f)) return false;
    m_level = level;

    this->setTitle("Colaborar en tiempo real");

    m_status = CCLabelBMFont::create("", "bigFont.fnt");
    m_status->setScale(0.4f);
    m_mainLayer->addChildAtPosition(m_status, Anchor::Center, {0.f, 95.f});

    m_roomInfo = CCLabelBMFont::create("", "chatFont.fnt");
    m_roomInfo->setScale(0.65f);
    m_roomInfo->setColor({150, 255, 150});
    m_mainLayer->addChildAtPosition(m_roomInfo, Anchor::Center, {0.f, 72.f});

    m_empty = CCLabelBMFont::create(
        "No hay nadie conectado.\nPide a tus amigos que abran el juego\ncon el mod y el mismo servidor.",
        "chatFont.fnt"
    );
    m_empty->setScale(0.7f);
    m_mainLayer->addChildAtPosition(m_empty, Anchor::Center, {0.f, 10.f});

    auto leftSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    leftSpr->setScale(0.7f);
    m_prev = CCMenuItemSpriteExtra::create(leftSpr, this, menu_selector(FriendsPopup::onPrev));
    m_buttonMenu->addChildAtPosition(m_prev, Anchor::Center, {-155.f, -15.f});

    auto rightSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    rightSpr->setScale(0.7f);
    rightSpr->setFlipX(true);
    m_next = CCMenuItemSpriteExtra::create(rightSpr, this, menu_selector(FriendsPopup::onNext));
    m_buttonMenu->addChildAtPosition(m_next, Anchor::Center, {155.f, -15.f});

    m_pageLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_pageLabel->setScale(0.6f);
    m_mainLayer->addChildAtPosition(m_pageLabel, Anchor::Center, {0.f, -95.f});

    this->schedule(schedule_selector(FriendsPopup::refresh), 0.25f);
    this->refresh(0.f);
    return true;
}

void FriendsPopup::refresh(float) {
    unsigned v = Collab::get().version();
    if (v != m_seen) {
        m_seen = v;
        this->rebuild();
    }
}

void FriendsPopup::rebuild() {
    auto& c = Collab::get();

    for (auto node : m_rows) node->removeFromParent();
    m_rows.clear();
    m_shown.clear();

    // estado de la conexion
    if (c.online()) {
        m_status->setString(("Conectado como " + c.myName()).c_str());
        m_status->setColor({120, 255, 120});
    } else {
        m_status->setString("Sin conexion al servidor");
        m_status->setColor({255, 120, 120});
    }

    // estado de la sala
    if (c.inRoom()) {
        std::string names;
        for (auto const& n : c.members()) {
            if (!names.empty()) names += ", ";
            names += n;
        }
        m_roomInfo->setString(
            ("En sala (" + std::to_string(c.members().size()) + "/6): " + names).c_str()
        );
    } else {
        m_roomInfo->setString("");
    }

    // lista de conectados con paginas
    auto const& users = c.users();
    int total = static_cast<int>(users.size());
    int pages = std::max(1, (total + PER_PAGE - 1) / PER_PAGE);
    m_page = std::clamp(m_page, 0, pages - 1);

    m_empty->setVisible(total == 0 && c.online());
    m_prev->setVisible(pages > 1);
    m_next->setVisible(pages > 1);
    m_pageLabel->setString(pages > 1 ? (std::to_string(m_page + 1) + "/" + std::to_string(pages)).c_str() : "");

    int first = m_page * PER_PAGE;
    int last = std::min(total, first + PER_PAGE);
    for (int i = first; i < last; ++i) {
        int r = i - first;
        float y = 45.f - static_cast<float>(r) * 30.f;
        std::string const& user = users[i];

        auto name = CCLabelBMFont::create(user.c_str(), "bigFont.fnt");
        name->setAnchorPoint({0.f, 0.5f});
        float scale = 0.5f;
        float width = name->getContentSize().width;
        if (width * scale > 170.f) scale = 170.f / width;
        name->setScale(scale);
        m_mainLayer->addChildAtPosition(name, Anchor::Center, {-140.f, y});
        m_rows.push_back(name);

        bool inside = std::find(c.members().begin(), c.members().end(), user) != c.members().end();
        if (inside) {
            auto tag = CCLabelBMFont::create("En la sala", "chatFont.fnt");
            tag->setScale(0.7f);
            tag->setColor({150, 255, 150});
            m_mainLayer->addChildAtPosition(tag, Anchor::Center, {105.f, y});
            m_rows.push_back(tag);
        } else {
            auto spr = ButtonSprite::create("Invitar", "bigFont.fnt", "GJ_button_01.png", 0.6f);
            auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(FriendsPopup::onInvite));
            btn->setTag(static_cast<int>(m_shown.size()));
            m_buttonMenu->addChildAtPosition(btn, Anchor::Center, {105.f, y});
            m_rows.push_back(btn);
        }
        m_shown.push_back(user);
    }

    // boton de abajo: depende de la situacion
    int mode = 0;
    if (!c.online()) mode = 1;
    else if (c.inRoom()) mode = 2;
    else if (!c.lastRoom().empty()) mode = 3;

    if (mode != m_mode) {
        if (m_action) m_action->removeFromParent();
        m_action = nullptr;
        m_mode = mode;

        char const* text = nullptr;
        if (mode == 1) text = "Reconectar";
        else if (mode == 2) text = "Salir de la sala";
        else if (mode == 3) text = "Volver a la sala";

        if (text) {
            auto spr = ButtonSprite::create(text, "bigFont.fnt", "GJ_button_02.png", 0.6f);
            m_action = CCMenuItemSpriteExtra::create(spr, this, menu_selector(FriendsPopup::onAction));
            m_buttonMenu->addChildAtPosition(m_action, Anchor::Center, {0.f, -112.f});
        }
    }
}

void FriendsPopup::onInvite(CCObject* sender) {
    int idx = static_cast<CCNode*>(sender)->getTag();
    if (idx < 0 || idx >= static_cast<int>(m_shown.size())) return;

    auto& c = Collab::get();
    if (!c.hostLevel(m_level)) return;
    c.invite(m_shown[idx]);
}

void FriendsPopup::onAction(CCObject*) {
    auto& c = Collab::get();
    switch (m_mode) {
        case 1: c.reconnect(); break;
        case 2: c.leaveRoom(); break;
        case 3: c.joinRoom(c.lastRoom()); break;
        default: break;
    }
}

void FriendsPopup::onPrev(CCObject*) {
    m_page--;
    m_seen = ~0u;
    this->refresh(0.f);
}

void FriendsPopup::onNext(CCObject*) {
    m_page++;
    m_seen = ~0u;
    this->refresh(0.f);
}
