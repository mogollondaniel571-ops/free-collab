#pragma once

#include <Geode/Geode.hpp>

#include <string>
#include <vector>

// Ventana "Colaborar": muestra a los conectados y permite invitarlos.
class FriendsPopup : public geode::Popup {
protected:
    geode::Ref<GJGameLevel> m_level;

    cocos2d::CCLabelBMFont* m_status = nullptr;
    cocos2d::CCLabelBMFont* m_roomInfo = nullptr;
    cocos2d::CCLabelBMFont* m_empty = nullptr;
    cocos2d::CCLabelBMFont* m_pageLabel = nullptr;
    CCMenuItemSpriteExtra* m_prev = nullptr;
    CCMenuItemSpriteExtra* m_next = nullptr;
    CCMenuItemSpriteExtra* m_action = nullptr;

    std::vector<cocos2d::CCNode*> m_rows;
    std::vector<std::string> m_shown;
    int m_page = 0;
    int m_mode = -1;
    unsigned m_seen = ~0u;

    bool init(GJGameLevel* level);
    void refresh(float dt);
    void rebuild();
    void onInvite(cocos2d::CCObject* sender);
    void onAction(cocos2d::CCObject* sender);
    void onPrev(cocos2d::CCObject* sender);
    void onNext(cocos2d::CCObject* sender);

public:
    static FriendsPopup* create(GJGameLevel* level);
};
