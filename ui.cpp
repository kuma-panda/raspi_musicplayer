#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sstream>

#include "ui.h"

//==============================================================================
//   TouchManager
//==============================================================================
//   コンストラクタ
//------------------------------------------------------------------------------
TouchManager::TouchManager() : m_thread(NULL), m_terminated(false)
{
     m_fd = open("/dev/input/event0", O_RDONLY);
     if( m_fd < 0 )
     {
          printf("Cannot open /dev/input/event0\n");
     }
}

//------------------------------------------------------------------------------
//   デストラクタ
//------------------------------------------------------------------------------
TouchManager::~TouchManager()
{
     m_terminated = true;
     if( m_thread )
     {
          m_thread->join();
          delete m_thread;
          close(m_fd);
     }
}

//------------------------------------------------------------------------------
//   イベント監視の開始
//------------------------------------------------------------------------------
void TouchManager::run()
{
     if( m_fd < 0 )
     {
          return;
     }
     m_thread = new std::thread([this](){ execute(); });
}

//------------------------------------------------------------------------------
//   イベントの監視（バックグラウンドスレッドで実行）
//------------------------------------------------------------------------------
void TouchManager::execute()
{
     int16_t x = -1, y = -1;
     bool touched = false;

     while( !m_terminated )
     {
          fd_set mask;
          struct timeval timeout;
          FD_ZERO(&mask);
          FD_SET(m_fd, &mask);
          timeout.tv_sec = 0;
          timeout.tv_usec = 20000;
          int ret = select(m_fd+1, &mask, NULL, NULL, &timeout);
          if( ret > 0 && FD_ISSET(m_fd, &mask) )
          {
               input_event ev;
               read(m_fd, &ev, sizeof(ev));
               m_mutex.lock();
               switch( ev.type )
               {
                    case EV_KEY:   // (1)タッチパネルの「押された」「離された」を検出
                         if( ev.code == BTN_TOUCH )    // 330
                         {
                              if( ev.value )
                              {
                                   touched = true;
                                   x = y = -1;
                              }
                              else
                              {
                                   touched = false;
                                   printf("[TouchManager] released\n");
                                   m_events.push_back(TouchEvent(false));
                              }
                         }
                         break;
                    case EV_ABS:   // 3
                         if( ev.code == ABS_X )
                         {
                              x = ev.value;
                         }
                         if( ev.code == ABS_Y )
                         {
                              y = ev.value;
                         }
                         if( touched && x >= 0 && y >= 0 )
                         {
                              printf("[TouchManager] touched (%hd, %hd)\n", x, y);
                              m_events.push_back(TouchEvent(true, x, y));
                              touched = false;
                         }
               }
               m_mutex.unlock();
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
     }
}

//------------------------------------------------------------------------------
//   イベントリスナを「プッシュ」する
//   イベントを受け取れるのは常に最後にプッシュされたウィジェットのみとなる
//------------------------------------------------------------------------------
void TouchManager::pushEventListener(UIWidget *widget)
{
     if( !m_listeners.empty() )
     {
          m_listeners.front()->setActive(false);
     }
     m_listeners.push_front(widget);
     widget->setActive(true);
}

//------------------------------------------------------------------------------
//   イベントリスナを「ポップ」する
//------------------------------------------------------------------------------
UIWidget *TouchManager::popEventListener()
{
     if( m_listeners.empty() )
     {
          return NULL;
     }

     UIWidget *w = m_listeners.front();
     m_listeners.pop_front();
     w->setActive(false);
     w->hide();
     if( !m_listeners.empty() )
     {
          m_listeners.front()->setActive(true);
          m_listeners.front()->refresh();
     }
     return w;
}


//------------------------------------------------------------------------------
//   マウスイベントをディスパッチする
//   これはメインスレッドで定期的に実行する必要がある
//------------------------------------------------------------------------------
void TouchManager::dispatchEvent()
{
     m_mutex.lock();
     if( m_events.empty() )
     {
          m_mutex.unlock();
          return;
     }

     TouchEvent e = m_events.front();
     m_events.pop_front();
     m_mutex.unlock();
     if( m_listeners.empty() )
     {
          return;
     }
     UIWidget *target = m_listeners.front();
     target->handleTouchEvent(e);
}



//==============================================================================
GraphicsPI UIWidget::m_gfx;

//------------------------------------------------------------------------------
//   コンストラクタ
//------------------------------------------------------------------------------
UIWidget::UIWidget(uint16_t id, UIWidget *parent)
     : m_id(id), m_parent(parent), m_enable(true), m_visible(true),
     m_captured(false), m_active(true)
{
     if( parent )
     {
          parent->addChild(this);
     }

     // 以下の２つのイベントは，自身が受けるイベントとして必ず登録する必要がある
     m_events[EVENT_TOUCHED] = [this](UIWidget *sender, int32_t p1, int32_t p2){
          onTouched((int16_t)p1, (int16_t)p2);
     };
     m_events[EVENT_RELEASED] = [this](UIWidget *sender, int32_t p1, int32_t p2){
          onReleased();
     };
}

//------------------------------------------------------------------------------
//   デストラクタ
//------------------------------------------------------------------------------
UIWidget::~UIWidget()
{
     for( int n = 0 ; n < (int)m_children.size() ; n++ )
     {
          delete m_children[n];
     }
}

//------------------------------------------------------------------------------
//   子ウィジェットを追加
//------------------------------------------------------------------------------
void UIWidget::addChild(UIWidget *child)
{
     m_children.push_back(child);
}

//------------------------------------------------------------------------------
//   位置・サイズを指定して生成
//------------------------------------------------------------------------------
void UIWidget::create(int16_t left, int16_t top, int16_t width, int16_t height)
{
     m_position.setPoint(left, top);
     m_clientRect.setRect(0, 0, width, height);
     Point pt = m_clientRect.topLeft();
     m_screenOffset = clientToScreen(pt);
}

//------------------------------------------------------------------------------
//   タッチされた時の処理
//   （派生クラスでオーバーライド）
//   (x, y) : タッチ位置の座標（クライアント座標単位）
//------------------------------------------------------------------------------
void UIWidget::onTouched(int16_t x, int16_t y)
{
}

//------------------------------------------------------------------------------
//   離された時の処理
//   （派生クラスでオーバーライド）
//------------------------------------------------------------------------------
void UIWidget::onReleased()
{
}

//------------------------------------------------------------------------------
//   クライアント座標（自身の左上隅を(0, 0)とする座標）における点 pt の座標を
//   画面座標に変換する
//------------------------------------------------------------------------------
Point UIWidget::clientToScreen(Point& pt)
{
     Point delta = m_position;
     UIWidget *parent = this->m_parent;
     while( parent )
     {
          delta = parent->clientToParent(delta);
          parent = parent->m_parent;
     }
     return pt.clone().offset(delta.x, delta.y);
}

//------------------------------------------------------------------------------
//   クライアント座標（自身の左上隅を(0, 0)とする座標）における点 pt の座標を
//   親ウイジェットのクライアント座標単位に変換する
//------------------------------------------------------------------------------
Point UIWidget::clientToParent(Point& pt)
{
     return pt.clone().offset(m_position.x, m_position.y);
}

//------------------------------------------------------------------------------
//   画面座標を，自身のクライアント座標単位に変換する
//------------------------------------------------------------------------------
Point UIWidget::screenToClient(Point& pt)
{
     Point delta(0, 0);
     delta = clientToScreen(delta);
     return pt.clone().offset(-delta.x, -delta.y);
}

//------------------------------------------------------------------------------
//   タッチイベントの処理
//   touched : 「タッチされた」イベントであれば true,
//             「話された」イベントであれば false
//   pos : タッチされた位置（画面座標）
//   戻り値 : このイベントを処理した場合は true，そうでなければ false
//------------------------------------------------------------------------------
bool UIWidget::handleTouchEvent(TouchEvent& e)
{
     // 最初に子ウィジェットに処理させてみる
     for( int n = 0 ; n < (int)m_children.size() ; n++ )
     {
          if( m_children[n]->handleTouchEvent(e) )
          {
               return true;
          }
     }

     if( e.touched )
     {
          if( !isEnabled() || !isVisible() )
          {
               return false;
          }
          Point p = screenToClient(e.pos);
          if( m_clientRect.include(p) )
          {
               m_captured = true;
               triggerEvent(EVENT_TOUCHED, p.x, p.y);
               return true;
          }
     }
     else
     {
          if( m_captured )
          {
               m_captured = false;
               triggerEvent(EVENT_RELEASED);
               return true;
          }
     }
     return false;
}

//------------------------------------------------------------------------------
//   イベントハンドラを登録
//------------------------------------------------------------------------------
void UIWidget::attachEvent(uint16_t event, EventHandler handler)
{
     m_events[event] = handler;
}

//------------------------------------------------------------------------------
//   イベントを発火させる
//------------------------------------------------------------------------------
void UIWidget::triggerEvent(uint16_t event, int32_t param1, int32_t param2)
{
     EventMap::iterator f = m_events.find(event);
     if( f == m_events.end() )
     {
          return;
     }
     (f->second)(this, param1, param2);
}

//------------------------------------------------------------------------------
//   ウィジェットの再描画を促す
//------------------------------------------------------------------------------
void UIWidget::refresh()
{
     if( !m_visible )
     {
          return;
     }
     draw();
     for( int n = 0 ; n < (int)m_children.size() ; n++ )
     {
          m_children[n]->refresh();
     }
}

//------------------------------------------------------------------------------
//   ウィジェットの有効・無効化
//------------------------------------------------------------------------------
void UIWidget::enable()
{
     m_enable = true;
}
void UIWidget::disable()
{
     m_enable = false;
}
bool UIWidget::isEnabled()
{
     if( m_enable )
     {
          if( !m_parent || m_parent->isEnabled() )
          {
               return true;
          }
     }
     return false;
}
bool UIWidget::isActive()
{
     if( m_active )
     {
          if( !m_parent || m_parent->isActive() )
          {
               return true;
          }
     }
     return false;
}

//------------------------------------------------------------------------------
//   ウイジェットの表示・非表示化
//------------------------------------------------------------------------------
void UIWidget::show()
{
     m_visible = true;
}
void UIWidget::hide()
{
     m_visible = false;
}
bool UIWidget::isVisible()
{
     if( m_visible )
     {
          if( !m_parent || m_parent->isVisible() )
          {
               return true;
          }
     }
     return false;
}

//------------------------------------------------------------------------------
UIWidget *UIWidget::getChildByID(uint16_t id)
{
     for( int n = 0 ; n < (int)m_children.size() ; n++ )
     {
          if( m_children[n]->getID() == id )
          {
               return m_children[n];
          }
     }
     return NULL;
}

//------------------------------------------------------------------------------
//   描画
//   基本クラスでは何も行わないので，派生クラスでオーバーライドする
//------------------------------------------------------------------------------
void UIWidget::draw()
{

}

//------------------------------------------------------------------------------
//   描画関連メソッド
//   これらはすべてクライアント座標を渡すことができる
//   GraphicsPI のメソッドは直接使わない
//------------------------------------------------------------------------------
void UIWidget::clear(uint16_t color)
{
     Rect r = offsetToScreen(m_clientRect);
     m_gfx.fillRect(r, color);
}

//------------------------------------------------------------------------------
void UIWidget::putPixel(Point& pt, uint16_t color)
{
     Point p = offsetToScreen(pt);
     m_gfx.putPixel(p, color);
}
//------------------------------------------------------------------------------
void UIWidget::fillRect(Rect& rc, uint16_t color)
{
     Rect r = offsetToScreen(rc);
     m_gfx.fillRect(r, color);
}

//------------------------------------------------------------------------------
void UIWidget::drawRect(Rect& rc, uint16_t color)
{
     Rect r = offsetToScreen(rc);
     m_gfx.drawRect(r, color);
}

//------------------------------------------------------------------------------
void UIWidget::drawFastHLine(Point& pt, int16_t len, uint16_t color)
{
     Point p = offsetToScreen(pt);
     m_gfx.drawFastHLine(p, len, color);
}

//------------------------------------------------------------------------------
void UIWidget::drawFastVLine(Point& pt, int16_t len, uint16_t color)
{
     Point p = offsetToScreen(pt);
     m_gfx.drawFastVLine(p, len, color);
}

//------------------------------------------------------------------------------
void UIWidget::drawLine(Point& p0, Point& p1, uint16_t color)
{
     Point pp0 = offsetToScreen(p0);
     Point pp1 = offsetToScreen(p1);
     m_gfx.drawLine(pp0, pp1, color);
}

//------------------------------------------------------------------------------
void UIWidget::drawCircle(Point& pt, int16_t r, uint16_t color)
{
     Point p = offsetToScreen(pt);
     m_gfx.drawCircle(p, r, color);
}

//------------------------------------------------------------------------------
void UIWidget::fillCircle(Point& pt, int16_t r, uint16_t color)
{
     Point p = offsetToScreen(pt);
     m_gfx.drawCircle(p, r, color);
}

//------------------------------------------------------------------------------
void UIWidget::drawRoundRect(Rect& rc, int16_t radius, uint16_t color)
{
     Rect r = offsetToScreen(rc);
     m_gfx.drawRoundRect(r, radius, color);
}

//------------------------------------------------------------------------------
void UIWidget::fillRoundRect(Rect& rc, int16_t radius, uint16_t color)
{
     Rect r = offsetToScreen(rc);
     m_gfx.fillRoundRect(r, radius, color);
}

//------------------------------------------------------------------------------
void UIWidget::selectFont(int size)
{
     m_gfx.selectFont(size);
}

//------------------------------------------------------------------------------
void UIWidget::drawChar(Point& pt, uint16_t code, uint16_t color)
{
     Point p = offsetToScreen(pt);
     m_gfx.drawChar(p, code, color);
}

//------------------------------------------------------------------------------
void UIWidget::drawText(Point& pt, const char *str, uint16_t color)
{
     Point p = offsetToScreen(pt);
     m_gfx.drawText(p, str, color);
}

//------------------------------------------------------------------------------
int16_t UIWidget::getTextWidth(const char *str)
{
     return m_gfx.getTextWidth(str);
}

//------------------------------------------------------------------------------
int16_t UIWidget::getTextHeight()
{
     return m_gfx.getTextHeight();
}

//------------------------------------------------------------------------------
void UIWidget::drawText(Rect& rc, const char *str, uint8_t align, uint16_t fgcol)
{
     Rect r = offsetToScreen(rc);
     m_gfx.drawText(r, str, align, fgcol);
}

//------------------------------------------------------------------------------
void UIWidget::drawText(Rect& rc, const char *str, uint8_t align, uint16_t fgcol, uint16_t bkcol)
{
     Rect r = offsetToScreen(rc);
     m_gfx.drawText(r, str, align, fgcol, bkcol);
}

//------------------------------------------------------------------------------
void UIWidget::drawImage(Rect& rc, std::vector<uint16_t>& image)
{
     Rect r = offsetToScreen(rc);
     m_gfx.drawImage(r, image);
}

//------------------------------------------------------------------------------
void UIWidget::getImage(Rect& rc, std::vector<uint16_t>& image)
{
     Rect r = offsetToScreen(rc);
     m_gfx.getImage(r, image);
}



//==============================================================================
Desktop::Desktop() : UIWidget(0, NULL)
{
     m_visible = false;
     Rect r = m_gfx.getScreenRect();
     create(r.left, r.top, r.width, r.height);
}

//==============================================================================
void Desktop::draw()
{
     clear(DEFAULT_FACE_COLOR);
}



//==============================================================================
//   Button
//==============================================================================
const uint16_t Button::CONTROL_COLOR[3] = {
     UIWidget::DEFAULT_CONTROL_COLOR,
     0x4382,
     0xC142
};
const uint16_t Button::PRESSED_COLOR[3] = {
     UIWidget::DEFAULT_PRESSED_COLOR,
     0x6CC6,
     0xEA45
};

Button::Button(uint16_t id, UIWidget *parent, uint8_t fontsize, uint8_t type)
     : UIWidget(id, parent), m_fontSize(fontsize), m_type(type)
{
}

//------------------------------------------------------------------------------
void Button::onTouched(int16_t x, int16_t y)
{
     UIWidget::onTouched(x, y);
     draw();
}

//------------------------------------------------------------------------------
void Button::onReleased()
{
     UIWidget::onReleased();
     draw();
     triggerEvent(EVENT_CLICKED);
}

//------------------------------------------------------------------------------
void Button::setCaption(std::string caption)
{
     m_caption = caption;
     refresh();
}

//------------------------------------------------------------------------------
void Button::draw()
{
     selectFont(m_fontSize);
     if( m_captured )
     {
          fillRoundRect(m_clientRect, 6, PRESSED_COLOR[m_type]);
          drawRoundRect(m_clientRect, 6, DEFAULT_BORDER_COLOR);
          drawText(m_clientRect, m_caption.c_str(), ALIGN_CENTER|ALIGN_MIDDLE, DEFAULT_TEXT_COLOR);
     }
     else if( isEnabled() )
     {
          fillRoundRect(m_clientRect, 6, CONTROL_COLOR[m_type]);
          drawRoundRect(m_clientRect, 6, DEFAULT_BORDER_COLOR);
          drawText(m_clientRect, m_caption.c_str(), ALIGN_CENTER|ALIGN_MIDDLE, DEFAULT_TEXT_COLOR);
     }
     else
     {
          fillRoundRect(m_clientRect, 6, DEFAULT_DISABLED_FACE_COLOR);
          drawRoundRect(m_clientRect, 6, DEFAULT_BORDER_COLOR);
          drawText(m_clientRect, m_caption.c_str(), ALIGN_CENTER|ALIGN_MIDDLE, DEFAULT_DISABLED_TEXT_COLOR);
     }
}


//==============================================================================
//   Panel
//==============================================================================
Panel::Panel(uint16_t id, UIWidget *parent) : UIWidget(id, parent),
     m_borderColor(DEFAULT_BORDER_COLOR), m_backColor(DEFAULT_CONTAINER_COLOR)
{
     m_showBorder[BORDER_LEFT] = true;
     m_showBorder[BORDER_TOP] = true;
     m_showBorder[BORDER_RIGHT] = true;
     m_showBorder[BORDER_BOTTOM] = true;
}

//------------------------------------------------------------------------------
void Panel::setColor(uint16_t back, uint16_t border)
{
     m_backColor = back;
     m_borderColor = border;
     refresh();
}

//------------------------------------------------------------------------------
void Panel::setBorder(bool left, bool top, bool right, bool bottom)
{
     m_showBorder[BORDER_LEFT] = left;
     m_showBorder[BORDER_TOP] = top;
     m_showBorder[BORDER_RIGHT] = right;
     m_showBorder[BORDER_BOTTOM] = bottom;
     refresh();
}

//------------------------------------------------------------------------------
void Panel::draw()
{
     Rect r = m_clientRect.clone();
     Point p;
     if( m_showBorder[BORDER_LEFT] )
     {
          p.setPoint(r.left, r.top);
          drawFastVLine(p, r.height, m_borderColor);
          r.offset(1, 0).resizeWidth(r.width-1);
     }
     if( m_showBorder[BORDER_TOP] )
     {
          p.setPoint(r.left, r.top);
          drawFastHLine(p, r.width, m_borderColor);
          r.offset(0, 1).resizeHeight(r.height-1);
     }
     if( m_showBorder[BORDER_RIGHT] )
     {
          p.setPoint(r.left+r.width-1, r.top);
          drawFastVLine(p, r.height, m_borderColor);
          r.resizeWidth(r.width-1);
     }
     if( m_showBorder[BORDER_BOTTOM] )
     {
          p.setPoint(r.left, r.top+r.height-1);
          drawFastHLine(p, r.width, m_borderColor);
          r.resizeHeight(r.height-1);
     }
     fillRect(r, m_backColor);
}


//==============================================================================
//   Tabbar
//==============================================================================
Tabbar::Tabbar(uint16_t id, UIWidget *parent, uint8_t fontsize)
     : UIWidget(id, parent), m_selectedIndex(-1), m_fontSize(fontsize)
{

}

//------------------------------------------------------------------------------
void Tabbar::addTab(uint16_t id, std::string label, int16_t width)
{
     Rect r;
     if( !m_tabs.empty() )
     {
          r = m_tabs.back().rect;
          r = r.clone().offset(r.width + 4, 0);
     }
     if( width <= 0 )
     {
          width = getTextWidth(label.c_str()) + 16;
     }
     r.resizeWidth(width);
     r.resizeHeight(m_clientRect.height);
     m_tabs.push_back(TabItem(id, r, label));
     m_selectedIndex = 0;
}

//------------------------------------------------------------------------------
void Tabbar::onTouched(int16_t x, int16_t y)
{
     UIWidget::onTouched(x, y);
     for( int n = 0 ; n < (int)m_tabs.size() ; n++ )
     {
          if( m_tabs[n].rect.include(Point(x, y)) && n != m_selectedIndex )
          {
               m_tabs[n].press();
               draw();
               break;
          }
     }
}

//------------------------------------------------------------------------------
void Tabbar::onReleased()
{
     UIWidget::onReleased();
     bool changed = false;
     for( int n = 0 ; n < (int)m_tabs.size() ; n++ )
     {
          if( m_tabs[n].release() )
          {
               changed = true;
               m_selectedIndex = n;
          }
     }
     draw();
     if( changed )
     {
          triggerEvent(EVENT_SELECT_CHANGED, m_selectedIndex);
     }
}

//------------------------------------------------------------------------------
void Tabbar::draw()
{
     selectFont(m_fontSize);

     Point p(m_clientRect.left, m_clientRect.top+m_clientRect.height-1);
     drawFastHLine(p, m_clientRect.width, DEFAULT_BORDER_COLOR);

     for( int n = 0 ; n < (int)m_tabs.size() ; n++ )
     {
          Rect r = m_tabs[n].rect.clone();
          if( n == m_selectedIndex )
          {
               drawRect(r, DEFAULT_BORDER_COLOR);
               r.inflate(-1, 0).offset(0, 1);
               fillRect(r, DEFAULT_CONTAINER_COLOR);
               drawText(r, m_tabs[n].label.c_str(), ALIGN_CENTER|ALIGN_MIDDLE, DEFAULT_TEXT_COLOR);
          }
          else
          {
               r.offset(0, 4).resizeHeight(r.height-4);
               drawRect(r, DEFAULT_BORDER_COLOR);
               r.inflate(-1, -1);
               if( m_tabs[n].down )
               {
                    fillRect(r, DEFAULT_PRESSED_COLOR);
               }
               else
               {
                    fillRect(r, NORMAL_TAB_COLOR);
               }
               drawText(r, m_tabs[n].label.c_str(), ALIGN_CENTER|ALIGN_MIDDLE, DEFAULT_TEXT_COLOR);
               r = m_tabs[n].rect.clone();
               r.resizeHeight(4);
               fillRect(r, DEFAULT_FACE_COLOR);
          }
     }
}

//------------------------------------------------------------------------------
void Tabbar::select(int index)
{
     m_selectedIndex = index;
     refresh();
}

//------------------------------------------------------------------------------
void Tabbar::selectByID(uint16_t id)
{
     for( int n = 0 ; n < (int)m_tabs.size() ; n++ )
     {
          if( m_tabs[n].id == id )
          {
               m_selectedIndex = n;
               refresh();
          }
     }
}

//------------------------------------------------------------------------------
uint16_t Tabbar::getSelectedID()
{
     if( m_selectedIndex <= 0 )
     {
          return 0;
     }
     return m_tabs[m_selectedIndex].id;
}


//==============================================================================
Label::Label(uint16_t id, UIWidget *parent, uint8_t fontsize)
     : UIWidget(id, parent),
     m_textColor(DEFAULT_TEXT_COLOR), m_backColor(DEFAULT_CONTAINER_COLOR),
     m_marginLR(4), m_marginTB(4),
     m_align(ALIGN_LEFT|ALIGN_MIDDLE), m_showBorder(true),
     m_fontSize(fontsize)
{

}

//------------------------------------------------------------------------------
void Label::onReleased()
{
     UIWidget::onReleased();
     triggerEvent(EVENT_CLICKED);
}

//------------------------------------------------------------------------------
void Label::setValue(std::string s)
{
     m_value = s;
     refresh();
}

//------------------------------------------------------------------------------
void Label::setColor(uint16_t text, uint16_t back)
{
     m_textColor = text;
     m_backColor = back;
     refresh();
}

//------------------------------------------------------------------------------
void Label::setMargin(int16_t lr, int16_t tb)
{
     m_marginLR = lr;
     m_marginTB = tb;
     refresh();
}

//------------------------------------------------------------------------------
void Label::setTextAlign(uint8_t align)
{
     m_align = align;
     refresh();
}

//------------------------------------------------------------------------------
void Label::setBorder(bool show)
{
     m_showBorder = show;
     refresh();
}

//------------------------------------------------------------------------------
void Label::draw()
{
     selectFont(m_fontSize);
     fillRect(m_clientRect, m_backColor);

     int16_t m = 0;
     if( m_showBorder )
     {
          drawRect(m_clientRect, DEFAULT_BORDER_COLOR);
          m = 1;
     }
     Rect r = m_clientRect.clone();
     r.inflate(-(m_marginLR+m), -(m_marginTB+m));
     drawText(r, m_value.c_str(), m_align, m_textColor);
}


//==============================================================================
//   ToggleButton
//==============================================================================
ToggleButton::ToggleButton(uint16_t id, UIWidget *parent, uint8_t fontsize)
     : UIWidget(id, parent), m_state(false), m_fontSize(fontsize)
{

}

//------------------------------------------------------------------------------
void ToggleButton::onTouched(int16_t x, int16_t y)
{
     UIWidget::onTouched(x, y);
     draw();
}

//------------------------------------------------------------------------------
void ToggleButton::onReleased()
{
     UIWidget::onReleased();
     m_state = !m_state;
     draw();
     triggerEvent(EVENT_CLICKED);
}

//------------------------------------------------------------------------------
void ToggleButton::setCaption(std::string caption)
{
     m_caption = caption;
     refresh();
}

//------------------------------------------------------------------------------
void ToggleButton::setState(bool b)
{
     if( m_state != b )
     {
          m_state = b;
          refresh();
     }
}

//------------------------------------------------------------------------------
void ToggleButton::draw()
{
     selectFont(m_fontSize);

     uint16_t backcolor, textcolor, lampcolor;

     if( m_captured )
     {
          backcolor = DEFAULT_PRESSED_COLOR;
          textcolor = DEFAULT_TEXT_COLOR;
     }
     else if( isEnabled() )
     {
          backcolor = DEFAULT_CONTROL_COLOR;
          textcolor = DEFAULT_TEXT_COLOR;
     }
     else
     {
          backcolor = DEFAULT_DISABLED_FACE_COLOR;
          textcolor = DEFAULT_DISABLED_TEXT_COLOR;
     }
     if( m_state )
     {
          lampcolor = COLOR_RED;
     }
     else
     {
          lampcolor = COLOR_BLACK;
     }

     fillRoundRect(m_clientRect, 6, backcolor);
     drawRoundRect(m_clientRect, 6, DEFAULT_BORDER_COLOR);
     Rect rcLamp(0, 0, 16, 16);
     rcLamp.setCenter(16, m_clientRect.height/2);
     fillRect(rcLamp, lampcolor);

     Rect rcText = m_clientRect.clone();
     rcText.resizeWidth(rcText.width - 40).offset(32, 0);
     drawText(rcText, m_caption.c_str(), ALIGN_LEFT|ALIGN_MIDDLE, textcolor);
}


//==============================================================================
//   PaintBox
//==============================================================================
PaintBox::PaintBox(uint16_t id, UIWidget *parent) : UIWidget(id, parent)
{

}

//------------------------------------------------------------------------------
void PaintBox::draw()
{
     triggerEvent(EVENT_PAINT);
}


//==============================================================================
//   MessageBox
//==============================================================================
const uint16_t MessageBox::TITLEBAR_COLOR[4] = {
     0x4382,   // GREEN
     0x918A,   // MAGENTA
     0x8363,   // YELLOW
     COLOR_RED
};
const char *MessageBox::TITLE[4] = {
     "情報", "確認", "警告", "エラー"
};

//------------------------------------------------------------------------------
MessageBox::MessageBox() : UIWidget(0, NULL), 
     m_style(MBS_INFO), m_touchManager(NULL)
{
     m_visible = false;

     m_okButton = new Button(0, this);
     m_okButton->setCaption("OK");
     m_okButton->attachEvent(EVENT_CLICKED, [this](UIWidget *sender, int32_t param1, int32_t param2){
          close(true);
     });
     m_cancelButton = new Button(1, this);
     m_cancelButton->setCaption("キャンセル");
     m_cancelButton->attachEvent(EVENT_CLICKED, [this](UIWidget *sender, int32_t param1, uint32_t param2){
          close(false);
     });
}

//------------------------------------------------------------------------------
void MessageBox::open(uint8_t style, std::string message, EventHandler handler)
{
     if( !m_touchManager || isVisible() )
     {
          return;
     }

     m_style = style;
     m_message = message;
     attachEvent(EVENT_CLOSE, handler);

     int16_t w = getTextWidth(message.c_str()) + 32;
     if( w < 300 ){ w = 300; }
     int16_t h = 8 + 32 + 16 + getTextHeight() + 20 + 32 + 8;
     Rect r = m_gfx.getScreenRect();
     int16_t x = (r.width - w)/2;
     int16_t y = (r.height - h)/2;
     create(x, y, w, h);

     Point pt = m_clientRect.bottomRight();
     pt.offset(-m_clientRect.width/2, -38);
     if( m_style == MBS_CONFIRM )
     {
          // ボタンは２つ
          m_okButton->create(pt.x-104, pt.y, 100, 30);
          m_cancelButton->create(pt.x+4, pt.y, 100, 30);
          m_cancelButton->show();
     }
     else
     {
          // ボタンは１つ
          m_okButton->create(pt.x-50, pt.y, 100, 30);
          m_cancelButton->hide();
     }

     m_touchManager->pushEventListener(this);
     show();
     refresh();
}

//------------------------------------------------------------------------------
void MessageBox::close(bool result)
{
     if( !m_touchManager || !isVisible() )
     {
          return;
     }
     m_touchManager->popEventListener();
     triggerEvent(EVENT_CLOSE, result? 1 : 0);
}

//------------------------------------------------------------------------------
void MessageBox::draw()
{
     selectFont(SMALL_FONT);
     fillRect(m_clientRect, DEFAULT_FACE_COLOR);
     Rect r = m_clientRect.clone();
     r.inflate(-2, -2);
     drawRect(r, DEFAULT_BORDER_COLOR);
     r.resizeHeight(32);
     drawRect(r, DEFAULT_BORDER_COLOR);
     r.inflate(-1, -1);
     drawText(r, TITLE[m_style], ALIGN_CENTER|ALIGN_MIDDLE, DEFAULT_TEXT_COLOR, TITLEBAR_COLOR[m_style]);
     r.offset(0, 45);
     drawText(r, m_message.c_str(), ALIGN_CENTER|ALIGN_MIDDLE, DEFAULT_TEXT_COLOR);
}

//------------------------------------------------------------------------------
MessageBox& MsgBox()
{
     static MessageBox msgbox;
     return msgbox;
}


//==============================================================================
//   NumberEditor
//==============================================================================
NumberEditor::NumberEditor() : UIWidget(0, NULL), m_touchManager(NULL)
{
     m_visible = false;

     Rect r = m_gfx.getScreenRect();
     int16_t w = 8+60+4+60+4+60+4+60+8;
     int16_t h = 8+40+4+48+4+48+4+48+4+48+8;
     int16_t x = (r.width - w)/2;
     int16_t y = (r.height - h)/2;
     create(x, y, w, h);

     Rect buttonRect[13] = {
          {8+60+4+60+4+60+4, 8+40+4+48+4,           60,      48+4+48},     // 0
          {8,                8+40+4+48+4+48+4,      60,      48},          // 1
          {8+60+4,           8+40+4+48+4+48+4,      60,      48},          // 2
          {8+60+4+60+4,      8+40+4+48+4+48+4,      60,      48},          // 3
          {8,                8+40+4+48+4,           60,      48},          // 4
          {8+60+4,           8+40+4+48+4,           60,      48},          // 5
          {8+60+4+60+4,      8+40+4+48+4,           60,      48},          // 6
          {8,                8+40+4,                60,      48},          // 7
          {8+60+4,           8+40+4,                60,      48},          // 8
          {8+60+4+60+4,      8+40+4,                60,      48},          // 9
          {8+60+4+60+4+60+4, 8+40+4,                60,      48},          // -
          {8,                8+40+4+48+4+48+4+48+4, 60+4+60, 48},          // OK
          {8+60+4+60+4,      8+40+4+48+4+48+4+48+4, 60+4+60, 48},          // CANCEL
     };
     const char *buttonCaption[13] = {
          "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "-", "OK", "CANCEL"
     };

     m_label = new Label(0, this, LARGE_FONT);
     m_label->create(9, 9, 60+4+60+4+60+4+60-2, 34);
     m_label->setColor(DEFAULT_TEXT_COLOR, COLOR_BLACK);
     m_label->setTextAlign(ALIGN_RIGHT|ALIGN_MIDDLE);
     m_label->setMargin(6, 6);

     for( int n = 0 ; n < 13 ; n++ )
     {
          m_buttons[n] = new Button(n, this, LARGE_FONT, (n >= 11)? BUTTONTYPE_FORM : BUTTONTYPE_NORMAL);
          m_buttons[n]->setCaption(buttonCaption[n]);
          m_buttons[n]->create(buttonRect[n].left, buttonRect[n].top, buttonRect[n].width, buttonRect[n].height);
          m_buttons[n]->attachEvent(EVENT_CLICKED, [this](UIWidget *sender, int32_t param1, int32_t param2){
               onButtonClick(sender);
          });
     }
}

//------------------------------------------------------------------------------
void NumberEditor::onButtonClick(UIWidget *sender)
{
     if( m_value.size() >= 8 )
     {
          return;
     }

     switch( sender->getID() )
     {
          case BUTTON_MINUS:
               if( m_value.empty() )
               {
                    m_value.push_back('-');
               }
               break;
          case 0:
               if( m_value.empty() )
               {
                    m_value.push_back(0);
               }
               else
               {
                    if( m_value[0] == '-' )
                    {
                         if( m_value.size() >= 2 )
                         {
                              m_value.push_back(0);
                         }
                    }
                    else if( m_value[0] != 0 )
                    {
                         m_value.push_back(0);
                    }
               }
               break;
          case BUTTON_OK:
               close(true);
               return;
          case BUTTON_CANCEL:
               close(false);
               return;
          default:
               if( m_value.empty() || m_value[0] != 0 )
               {
                    m_value.push_back(sender->getID());
               }
               break;
     }

     m_label->setValue(getDisplayStr());
     return;
}

//------------------------------------------------------------------------------
std::string NumberEditor::getDisplayStr()
{
     std::string s;
     for( int n = 0 ; n < (int)m_value.size() ; n++ )
     {
          if( m_value[n] == '-' )
          {
               s += '-';
          }
          else
          {
               s += ('0'+m_value[n]);
          }
     }
     if( s.length() == 0 )
     {
          s = "0";
     }
     return s;
}

//------------------------------------------------------------------------------
int32_t NumberEditor::getValue()
{
     int32_t v = 0;
     int32_t f = 1;
     for( int n = 0 ; n < (int)m_value.size() ; n++ )
     {
          if( m_value[n] == '-' )
          {
               f = -1;
          }
          else
          {
               v = v*10 + m_value[n];
          }
     }
     return v*f;
}

//------------------------------------------------------------------------------
void NumberEditor::open(EventHandler handler)
{
     if( !m_touchManager || isVisible() )
     {
          return;
     }
     attachEvent(EVENT_CLOSE, handler);
     m_value.clear();
     m_label->setValue(getDisplayStr());
     m_touchManager->pushEventListener(this);
     show();
     refresh();
}

//------------------------------------------------------------------------------
void NumberEditor::close(bool result)
{
     if( !m_touchManager || !isVisible() )
     {
          return;
     }
     m_touchManager->popEventListener();
     triggerEvent(EVENT_CLOSE, result? 1 : 0, getValue());
}

//------------------------------------------------------------------------------
void NumberEditor::draw()
{
     fillRect(m_clientRect, DEFAULT_FACE_COLOR);
     Rect r = m_clientRect.clone();
     r.inflate(-2, -2);
     drawRect(r, DEFAULT_BORDER_COLOR);
}

//------------------------------------------------------------------------------
NumberEditor& NumEdit()
{
     static NumberEditor editor;
     return editor;
}
