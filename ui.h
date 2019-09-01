#ifndef   UI_H
#define   UI_H

#include <cstdint>
#include <vector>
#include <map>
#include <deque>
#include <functional>
#include <string>
#include <thread>
#include <mutex>
#include "gfxpi.h"

//------------------------------------------------------------------------------
class TouchEvent
{
     public:
          bool touched;
          Point pos;
          TouchEvent() : touched(false){}
          TouchEvent(bool b, int16_t x = 0, int16_t y = 0) : touched(b), pos(x, y){}
          const TouchEvent& operator = (const TouchEvent& e)
          {
               touched = e.touched;
               pos = e.pos;
               return *this;
          }
};

//------------------------------------------------------------------------------
class UIWidget;
class TouchManager
{
     private:
          int m_fd;
          bool m_terminated;
          std::thread *m_thread;
          std::mutex m_mutex;
          std::deque<TouchEvent> m_events;
          std::deque<UIWidget *> m_listeners;

          void execute();

     public:
          TouchManager();
          ~TouchManager();
          void pushEventListener(UIWidget *widget);
          UIWidget *popEventListener();
          void run();
          void dispatchEvent();
};


//------------------------------------------------------------------------------
#define   EVENT_TOUCHED            1
#define   EVENT_RELEASED           2
#define   EVENT_CLICKED            3
#define   EVENT_SELECT_CHANGED     4
#define   EVENT_CLOSE              6
#define   EVENT_PAINT              7

//------------------------------------------------------------------------------
class UIWidget
{
     protected:
          typedef std::function<void(UIWidget *, int32_t, int32_t)>   EventHandler;
          typedef std::map<uint16_t, EventHandler>                    EventMap;
          enum{ DEFAULT_FACE_COLOR = 0x18C3 };              // 背景色（非常に暗いグレー）
          enum{ DEFAULT_CONTAINER_COLOR = 0x2104 };         // コンテナの背景色（暗いグレー）
          enum{ DEFAULT_BORDER_COLOR = 0x8C51 };            // 境界線の色（グレー）
          enum{ DEFAULT_TEXT_COLOR = 0xDEFB };              // 文字色
          enum{ DEFAULT_CONTROL_COLOR = 0x28CB };           // ボタンなどのコントロールの背景色
          enum{ DEFAULT_PRESSED_COLOR = 0x6292 };           // 「押されている」状態のコントロールの背景色
          enum{ DEFAULT_DISABLED_FACE_COLOR = 0x632C };     // 無効状態のコントロールの背景色
          enum{ DEFAULT_DISABLED_TEXT_COLOR = 0xAD55 };     // 無効状態のコントロールの文字色

          static GraphicsPI m_gfx;

          uint16_t  m_id;
          UIWidget *m_parent; // 親ウイジェット（NULLの場合もありうる）
          std::vector<UIWidget *> m_children;     // 子ウイジェットのリスト
          Point m_position;   // 親ウイジェットのクライアント座標における，自身の左上隅の座標
          Rect m_clientRect;  // クライアント矩形（top, left はつねにゼロ）
          bool m_enable;      // タッチイベントを受け取ることができれば true
          bool m_visible;     // 画面上に表示されるならば true
          bool m_captured;    // タッチイベントのキャプチャ中であれば true
          bool m_active;      // タッチイベントを受け取ることが可能であれば true
          EventMap m_events;

          void addChild(UIWidget *child);
          virtual void onTouched(int16_t x, int16_t y);
          virtual void onReleased();
          virtual void draw();

     private:
          Point m_screenOffset;    // 自身の左上隅座標を画面座標で表した値
          Rect offsetToScreen(Rect& r){
               return r.clone().offset(m_screenOffset.x, m_screenOffset.y);
          }
          Point offsetToScreen(Point& p){
               return p.clone().offset(m_screenOffset.x, m_screenOffset.y);
          }

     public:
          UIWidget(uint16_t id, UIWidget *parent = NULL);
          virtual ~UIWidget();
          virtual void create(int16_t left, int16_t top, int16_t width, int16_t height);
          virtual void enable();
          virtual void disable();
          virtual void show();
          virtual void hide();
          virtual void setActive(bool active){ m_active = active; }

          uint16_t getID() const { return m_id; }
          UIWidget *getChildByID(uint16_t id);

          bool isEnabled();
          bool isVisible();
          bool isActive();
          void refresh();

          Rect  getClientRect(){ return m_clientRect; }
          Point clientToScreen(Point& pt);
          Point clientToParent(Point& pt);
          Point screenToClient(Point& pt);
          bool handleTouchEvent(TouchEvent& e);
          void attachEvent(uint16_t event, EventHandler handler);
          void triggerEvent(uint16_t event, int32_t param1 = 0, int32_t param2 = 0);

          void clear(uint16_t color);
          void putPixel(Point& pt, uint16_t color);
          void fillRect(Rect& r, uint16_t color);
          void drawRect(Rect& r, uint16_t color);
          void drawFastHLine(Point& pt, int16_t len, uint16_t color);
          void drawFastVLine(Point& pt, int16_t len, uint16_t color);
          void drawLine(Point& p0, Point& p1, uint16_t color);
          void drawCircle(Point& p, int16_t r, uint16_t color);
          void fillCircle(Point& p, int16_t r, uint16_t color);
          void drawRoundRect(Rect& rc, int16_t r, uint16_t color);
          void fillRoundRect(Rect& rc, int16_t r, uint16_t color);
          void selectFont(int size);
          void drawChar(Point& p, uint16_t c, uint16_t color);
          void drawText(Point& p, const char *str, uint16_t color);
          int16_t getTextWidth(const char *str);
          int16_t getTextHeight();
          void drawText(Rect& r, const char *str, uint8_t align, uint16_t fgcol);
          void drawText(Rect& r, const char *str, uint8_t align, uint16_t fgcol, uint16_t bkcol);
          void drawImage(Rect& r, std::vector<uint16_t>& image);
          void getImage(Rect& r, std::vector<uint16_t>& image);

          static uint16_t RGBToColor(uint8_t r, uint8_t g, uint8_t b){
               uint16_t R5 = ((uint16_t)r * 249 + 1014) >> 11;
               uint16_t G6 = ((uint16_t)g * 253 +  505) >> 10;
               uint16_t B5 = ((uint16_t)b * 249 + 1014) >> 11;
               return (R5 << 11) + (G6 << 5) + B5;
          }
};

//------------------------------------------------------------------------------
class Desktop : public UIWidget
{
     protected:
          void draw();
     public:
          Desktop();
};

//------------------------------------------------------------------------------
#define   BUTTONTYPE_NORMAL   0
#define   BUTTONTYPE_FORM     1
#define   BUTTONTYPE_DANGER   2

class Button : public UIWidget
{
     private:
          std::string m_caption;
          uint8_t m_fontSize;
          uint8_t m_type;
          static const uint16_t CONTROL_COLOR[3];
          static const uint16_t PRESSED_COLOR[3];

     protected:
          void draw();
          void onTouched(int16_t x, int16_t y);
          void onReleased();

     public:
          enum {
          };
          Button(uint16_t id, UIWidget *parent, uint8_t fontsize = SMALL_FONT, uint8_t type = BUTTONTYPE_NORMAL);
          void setCaption(std::string str);
          std::string& getCaption(){ return m_caption; }
};

//------------------------------------------------------------------------------
class Panel : public UIWidget
{
     private:
          bool m_showBorder[4];
          uint16_t m_backColor;
          uint16_t m_borderColor;

     protected:
          void draw();

     public:
          enum {
               BORDER_LEFT = 0,
               BORDER_TOP = 1,
               BORDER_RIGHT = 2,
               BORDER_BOTTOM = 3
          };
          Panel(uint16_t id, UIWidget *parent);
          void setColor(uint16_t back, uint16_t border);
          void setBorder(bool left, bool top, bool right, bool bottom);
};

//------------------------------------------------------------------------------
class TabItem
{
     public:
          uint16_t id;
          Rect rect;
          bool down;
          std::string label;
          TabItem() : id(0){}
          TabItem(uint16_t i, const Rect& r, std::string l) : id(i), rect(r), label(l), down(false){}
          const TabItem& operator = (TabItem& r){
               id = r.id;
               rect = r.rect;
               label = r.label;
               return *this;
          }
          void press(){
               down = true;
          }
          bool release(){
               if( down )
               {
                    down = false;
                    return true;
               }
               return false;
          }
};

//------------------------------------------------------------------------------
class Tabbar : public UIWidget
{
     private:
          // enum{ BACK_COLOR = COLOR_MIDNIGHTBLUE };
          // enum{ TEXT_COLOR = COLOR_WHITE };
          // enum{ BORDER_COLOR = COLOR_SILVER };
          // enum{ PRESSED_COLOR = COLOR_DARKVIOLET };

          std::vector<TabItem> m_tabs;
          uint8_t m_fontSize;
          int m_selectedIndex;

     protected:
          void draw();
          void onTouched(int16_t x, int16_t y);
          void onReleased();

     public:
          // enum{ SELECTED_COLOR = 0x31A6 };
          enum{ NORMAL_TAB_COLOR = 0x31A6 };
          Tabbar(uint16_t id, UIWidget *parent, uint8_t fontsize = SMALL_FONT);
          void addTab(uint16_t id, std::string label, int16_t width = 0);
          void select(int index);
          void selectByID(uint16_t id);
          int getSelectedIndex() const { return m_selectedIndex; }
          uint16_t getSelectedID();
};

//------------------------------------------------------------------------------
class Label : public UIWidget
{
     private:
          std::string m_value;
          uint16_t m_backColor;
          uint16_t m_textColor;
          uint8_t m_align;
          int16_t m_marginLR;
          int16_t m_marginTB;
          uint8_t m_fontSize;
          bool m_showBorder;

     protected:
          void draw();
          void onReleased();

     public:
          Label(uint16_t id, UIWidget *parent, uint8_t fontsize = SMALL_FONT);
          void setValue(std::string s);
          std::string& getValue(){ return m_value; }
          void setColor(uint16_t text, uint16_t back);
          void setMargin(int16_t lr, int16_t tb);
          void setTextAlign(uint8_t align);
          void setBorder(bool show);
};

//------------------------------------------------------------------------------
class ToggleButton : public UIWidget
{
     private:
          std::string m_caption;
          uint8_t m_fontSize;
          bool m_state;

          enum{ NORMAL_FACE_COLOR = COLOR_SLATEBLUE };
          enum{ NORMAL_TEXT_COLOR = COLOR_WHITE };
          enum{ PRESSED_FACE_COLOR = COLOR_DARKVIOLET };
          enum{ PRESSED_TEXT_COLOR = COLOR_WHITE };
          enum{ DISABLED_FACE_COLOR = COLOR_MEDIUMORCHID };
          enum{ DISABLED_TEXT_COLOR = COLOR_DARKGRAY };
          enum{ BORDER_COLOR = COLOR_ALICEBLUE };

     protected:
          void draw();
          void onTouched(int16_t x, int16_t y);
          void onReleased();

     public:
          ToggleButton(uint16_t id, UIWidget *parent, uint8_t fontsize = SMALL_FONT);
          void setCaption(std::string caption);
          void setState(bool state);
          std::string& getCaption(){ return m_caption; }
          bool getState() const { return m_state; }
};

//------------------------------------------------------------------------------
class PaintBox : public UIWidget
{
     protected:
          void draw();
     public:
          PaintBox(uint16_t id, UIWidget *parent);
};

//------------------------------------------------------------------------------
#define   MBS_INFO       0    // 通常のアラート
#define   MBS_CONFIRM    1    // 確認用（OK, Cancel の２つのボタンを持つ）
#define   MBS_WARNING    2    // ワーニング用
#define   MBS_ERROR      3    // エラーメッセージ用

class MessageBox : public UIWidget
{
     friend MessageBox& MsgBox();
     private:
          enum{ FACE_COLOR = COLOR_MIDNIGHTBLUE };
          enum{ BORDER_COLOR = COLOR_SILVER };
          static const uint16_t TITLEBAR_COLOR[4];
          static const char *TITLE[4];
          uint8_t m_style;
          std::string m_message;
          Button *m_okButton;
          Button *m_cancelButton;
          TouchManager *m_touchManager;

          MessageBox();
          void close(bool result);

     protected:
          void draw();
     public:
          void initialize(TouchManager *touch){
               m_touchManager = touch;
          }
          void open(uint8_t style, std::string message, EventHandler handler);
};

MessageBox& MsgBox();

//------------------------------------------------------------------------------
class NumberEditor : public UIWidget
{
     friend NumberEditor& NumEdit();
     protected:
          void draw();

     private:
          enum{ FACE_COLOR = COLOR_MIDNIGHTBLUE };
          enum{ BORDER_COLOR = COLOR_SILVER };
          enum{
               BUTTON_MINUS  = 10,
               BUTTON_OK     = 11,
               BUTTON_CANCEL = 12,
          };
          std::vector<uint8_t> m_value;
          Button *m_buttons[13];
          Label  *m_label;
          TouchManager *m_touchManager;

          NumberEditor();
          void onButtonClick(UIWidget *sender);
          std::string getDisplayStr();

     public:
          void initialize(TouchManager *touch){
               m_touchManager = touch;
          }
          void open(EventHandler handler);
          void close(bool);
          int32_t getValue();
};

NumberEditor& NumEdit();


#endif
