#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include "gfxpi.h"


//------------------------------------------------------------------------------
GraphicsPI::GraphicsPI() : m_fbfd(-1), m_available(false)
{
     m_currentFont = &m_font[SMALL_FONT];

     m_fbfd = open("/dev/fb0", O_RDWR);
     if( m_fbfd == -1 )
     {
          printf("Error: cannot open framebuffer device.\n");
          return;
     }

     // Get variable screen information
     if( ioctl(m_fbfd, FBIOGET_VSCREENINFO, &m_vinfo) )
     {
          printf("Error reading variable information.\n");
          close(m_fbfd);
          m_fbfd = -1;
          return;
     }
     printf("Original: %d * %d (%d bpp)\n", m_vinfo.xres, m_vinfo.yres, m_vinfo.bits_per_pixel);

     // Store for reset (copy vinfo to vinfo_orig)
     memcpy(&m_orig_vinfo, &m_vinfo, sizeof(struct fb_var_screeninfo));

     // Change variable info
     // use: 'fbset -depth x' to test different bpps
     m_vinfo.bits_per_pixel = 16;    // 16bit color (RGB565)
     if( ioctl(m_fbfd, FBIOPUT_VSCREENINFO, &m_vinfo) )
     {
          printf("Error setting variable information.\n");
          close(m_fbfd);
          m_fbfd = -1;
          return;
     }

     // Get fixed screen information
     if( ioctl(m_fbfd, FBIOGET_FSCREENINFO, &m_finfo) )
     {
          printf("Error reading fixed information.\n");
          ioctl(m_fbfd, FBIOPUT_VSCREENINFO, &m_orig_vinfo);
          close(m_fbfd);
          m_fbfd = -1;
          return;
     }

     // map fb to user mem
     m_screenSize = m_finfo.smem_len;
     m_fbp = (uint16_t *)mmap(0, m_screenSize, PROT_READ|PROT_WRITE, MAP_SHARED, m_fbfd, 0);
     if( (int)m_fbp == -1 )
     {
          printf("Failed to mmap.\n");
          ioctl(m_fbfd, FBIOPUT_VSCREENINFO, &m_orig_vinfo);
          close(m_fbfd);
          m_fbfd = -1;
          return;
     }

     if( !loadFont() )
     {
          ioctl(m_fbfd, FBIOPUT_VSCREENINFO, &m_orig_vinfo);
          close(m_fbfd);
          m_fbfd = -1;
          return;
     }

     m_available = true;
}

//------------------------------------------------------------------------------
GraphicsPI::~GraphicsPI()
{
     if( m_fbfd >= 0 )
     {
          // cleanup
          // unmap fb file from memory
          munmap(m_fbp, m_screenSize);
          // reset the display mode
          ioctl(m_fbfd, FBIOPUT_VSCREENINFO, &m_orig_vinfo);
          // close fb file
          close(m_fbfd);
     }
}

//------------------------------------------------------------------------------
const char *GraphicsPI::FONTFILE_PATH[2] = {
     "./font/font20plus.dat",
     "./font/font16.dat"
};
const uint8_t GraphicsPI::FONT_HEIGHT[2] = {20, 16};

bool GraphicsPI::loadFont()
{
     uint8_t buf[128];    // 2+2+4*16 = 68
     for( int n = 0 ; n < 2 ; n++ )
     {
          FILE *fp = fopen(FONTFILE_PATH[n], "r");
          if( !fp )
          {
               printf("Unable to load \"%s\"\n", FONTFILE_PATH[n]);
               return false;
          }
          int bytelen = 2+2+4*(int)FONT_HEIGHT[n];
          while( !feof(fp) && (fread(buf, 1, bytelen, fp) == bytelen) )
          {
               uint16_t code = *((uint16_t *)buf);
               m_font[n][code].load(FONT_HEIGHT[n], (uint16_t *)buf);
               if( n == SMALL_FONT )
               {
                    ++(m_font[n][code].width);
               }
          }
          fclose(fp);
          printf("%s successfully loaded.\n", FONTFILE_PATH[n]);
     }
     return true;
}

//------------------------------------------------------------------------------
Rect GraphicsPI::getScreenRect()
{
     return Rect(0, 0, (int16_t)m_vinfo.xres, (int16_t)m_vinfo.yres);
}

//------------------------------------------------------------------------------
uint32_t GraphicsPI::offsetOfCoord(int16_t x, int16_t y)
{
     return ((uint32_t)x*2 + m_finfo.line_length*((uint32_t)y))/2;
}

//------------------------------------------------------------------------------
void GraphicsPI::putPixel(int16_t x, int16_t y, uint16_t color)
{
     if( !m_available ){ return; }

     // x = std::max(0, std::min(x, (int16_t)m_vinfo.xres-1));
     // y = std::max(0, std::min(y, (int16_t)m_vinfo.yres-1));

     // now this is about the same as 'fbp[pix_offset] = value'
     // but a bit more complicated for RGB565
     // unsigned short c = ((r / 8) << 11) + ((g / 4) << 5) + (b / 8);
     // or: c = ((r / 8) * 2048) + ((g / 4) * 32) + (b / 8);

     m_fbp[offsetOfCoord(x, y)] = color;
}

//------------------------------------------------------------------------------
void GraphicsPI::clear(uint16_t color)
{
     if( !m_available ){ return; }

     for( uint32_t n = 0 ; n < m_screenSize/2 ; n++ )
     {
          *(m_fbp + n) = color;
     }
}

//------------------------------------------------------------------------------
void GraphicsPI::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
     if( !m_available ){ return; }

     for( int16_t r = 0 ; r < h ; r++ )
     {
          uint32_t ofs = offsetOfCoord(x, y+r);
          uint16_t *p = m_fbp + ofs;
          for( uint n = 0 ; n < w ; n++ )
          {
               *p++ = color;
          }
     }
}

//------------------------------------------------------------------------------
void GraphicsPI::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
     if( !m_available ){ return; }

     for( int16_t r = 0 ; r < h ; r++ )
     {
          uint32_t ofs = offsetOfCoord(x, y+r);
          uint16_t *p = m_fbp + ofs;
          if( r == 0 || r == (h-1) )
          {
               for( uint n = 0 ; n < w ; n++ )
               {
                    *p++ = color;
               }
          }
          else
          {
               *p = color;
               *(p+w-1) = color;
          }
     }
}

//------------------------------------------------------------------------------
void GraphicsPI::drawFastHLine(int16_t x, int16_t y, int16_t len, uint16_t color)
{
     if( !m_available ){ return; }

     uint32_t ofs = offsetOfCoord(x, y);
     for( int16_t n = 0 ; n < len ; n++ )
     {
          *(m_fbp+ofs+n) = color;
     }
}

//------------------------------------------------------------------------------
void GraphicsPI::drawFastVLine(int16_t x, int16_t y, int16_t len, uint16_t color)
{
     if( !m_available ){ return; }

     for( int16_t r = 0 ; r < len ; r++ )
     {
          *(m_fbp+offsetOfCoord(x, y+r)) = color;
     }
}

//------------------------------------------------------------------------------
void GraphicsPI::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color)
{
     if( x0 == x1 )
     {
          if( y0 > y1 ) std::swap(y0, y1);
          drawFastVLine(x0, y0, y1 - y0 + 1, color);
          return;
     }
     else if( y0 == y1 )
     {
          if( x0 > x1 ) std::swap(x0, x1);
          drawFastHLine(x0, y0, x1 - x0 + 1, color);
          return;
     }

     int16_t steep = std::abs(y1 - y0) > std::abs(x1 - x0);
     if( steep )
     {
          std::swap(x0, y0);
          std::swap(x1, y1);
     }
     if (x0 > x1)
     {
          std::swap(x0, x1);
          std::swap(y0, y1);
     }

     int16_t dx, dy;
     dx = x1 - x0;
     dy = std::abs(y1 - y0);

     int16_t err = dx / 2;
     int16_t ystep;

     if( y0 < y1 )
     {
          ystep = 1;
     }
     else
     {
          ystep = -1;
     }

     for( ; x0 <= x1 ; x0++ )
     {
          if( steep )
          {
               putPixel(y0, x0, color);
          }
          else
          {
               putPixel(x0, y0, color);
          }
          err -= dy;
          if( err < 0 )
          {
               y0 += ystep;
               err += dx;
          }
     }
}

//------------------------------------------------------------------------------
void GraphicsPI::drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color)
{
     int16_t f = 1 - r;
     int16_t ddF_x = 1;
     int16_t ddF_y = -2 * r;
     int16_t x = 0;
     int16_t y = r;

     putPixel(x0  , y0+r, color);
     putPixel(x0  , y0-r, color);
     putPixel(x0+r, y0  , color);
     putPixel(x0-r, y0  , color);

     while( x < y )
     {
          if( f >= 0 )
          {
               y--;
               ddF_y += 2;
               f += ddF_y;
          }
          x++;
          ddF_x += 2;
          f += ddF_x;

          putPixel(x0 + x, y0 + y, color);
          putPixel(x0 - x, y0 + y, color);
          putPixel(x0 + x, y0 - y, color);
          putPixel(x0 - x, y0 - y, color);
          putPixel(x0 + y, y0 + x, color);
          putPixel(x0 - y, y0 + x, color);
          putPixel(x0 + y, y0 - x, color);
          putPixel(x0 - y, y0 - x, color);
     }
}

//------------------------------------------------------------------------------
void GraphicsPI::drawCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t cornername, uint16_t color)
{
     int16_t f     = 1 - r;
     int16_t ddF_x = 1;
     int16_t ddF_y = -2 * r;
     int16_t x     = 0;
     int16_t y     = r;

     while( x < y )
     {
          if( f >= 0 )
          {
               y--;
               ddF_y += 2;
               f += ddF_y;
          }
          x++;
          ddF_x += 2;
          f += ddF_x;
          if( cornername & 0x4 )
          {
               putPixel(x0 + x, y0 + y, color);
               putPixel(x0 + y, y0 + x, color);
          }
          if( cornername & 0x2 )
          {
               putPixel(x0 + x, y0 - y, color);
               putPixel(x0 + y, y0 - x, color);
          }
          if( cornername & 0x8 )
          {
               putPixel(x0 - y, y0 + x, color);
               putPixel(x0 - x, y0 + y, color);
          }
          if( cornername & 0x1 )
          {
               putPixel(x0 - y, y0 - x, color);
               putPixel(x0 - x, y0 - y, color);
          }
     }
}

//------------------------------------------------------------------------------
void GraphicsPI::fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color)
{
    drawFastVLine(x0, y0-r, 2*r+1, color);
    fillCircleHelper(x0, y0, r, 3, 0, color);
}

//------------------------------------------------------------------------------
void GraphicsPI::fillCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t corners, int16_t delta, uint16_t color)
{
     int16_t f     = 1 - r;
     int16_t ddF_x = 1;
     int16_t ddF_y = -2 * r;
     int16_t x     = 0;
     int16_t y     = r;
     int16_t px    = x;
     int16_t py    = y;

     delta++; // Avoid some +1's in the loop

     while( x < y )
     {
          if (f >= 0) {
               y--;
               ddF_y += 2;
               f += ddF_y;
          }
          x++;
          ddF_x += 2;
          f += ddF_x;
          // These checks avoid double-drawing certain lines, important
          // for the SSD1306 library which has an INVERT drawing mode.
          if( x < (y + 1) )
          {
               if(corners & 1) drawFastVLine(x0+x, y0-y, 2*y+delta, color);
               if(corners & 2) drawFastVLine(x0-x, y0-y, 2*y+delta, color);
          }
          if( y != py )
          {
               if(corners & 1) drawFastVLine(x0+py, y0-px, 2*px+delta, color);
               if(corners & 2) drawFastVLine(x0-py, y0-px, 2*px+delta, color);
               py = y;
          }
          px = x;
     }
}

//------------------------------------------------------------------------------
void GraphicsPI::drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color)
{
    int16_t max_radius = ((w < h) ? w : h) / 2; // 1/2 minor axis
    if( r > max_radius )
    {
         r = max_radius;
    }
    drawFastHLine(x+r  , y    , w-2*r, color); // Top
    drawFastHLine(x+r  , y+h-1, w-2*r, color); // Bottom
    drawFastVLine(x    , y+r  , h-2*r, color); // Left
    drawFastVLine(x+w-1, y+r  , h-2*r, color); // Right
    // draw four corners
    drawCircleHelper(x+r    , y+r    , r, 1, color);
    drawCircleHelper(x+w-r-1, y+r    , r, 2, color);
    drawCircleHelper(x+w-r-1, y+h-r-1, r, 4, color);
    drawCircleHelper(x+r    , y+h-r-1, r, 8, color);
}

//------------------------------------------------------------------------------
void GraphicsPI::fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color)
{
    int16_t max_radius = ((w < h) ? w : h) / 2; // 1/2 minor axis
    if( r > max_radius )
    {
         r = max_radius;
    }
    fillRect(x+r, y, w-2*r, h, color);
    fillCircleHelper(x+w-r-1, y+r, r, 1, h-2*r-1, color);
    fillCircleHelper(x+r    , y+r, r, 2, h-2*r-1, color);
}

//------------------------------------------------------------------------------
void GraphicsPI::selectFont(int size)
{
     m_currentFont = &m_font[size];
     // printf("Current font size (%d) : %d\n", size, (int)m_currentFont.begin()->second.height);
}


//------------------------------------------------------------------------------
int16_t GraphicsPI::drawChar(int16_t x, int16_t y, uint16_t code, uint16_t color)
{
     if( m_currentFont->find(code) == m_currentFont->end() )
     {
          return x;
     }

     Font& font = (*m_currentFont)[code];
     for( int16_t n = 0 ; (n < font.width) && (x < m_vinfo.xres) ; n++ )
     {
          for( int h = 0 ; h < font.height ; h++ )
          {
               if( y + h >= m_vinfo.yres )
               {
                    break;
               }
               if( font.data[h] & (0x80000000 >> n) )
               {
                    putPixel(x, y+h, color);
               }
          }
          ++x;
     }
     return x;
}

//------------------------------------------------------------------------------
//   UTF-8バイト列で，pの指す文字の文字コード(UCS-2)を取得する
//   取得した文字コードは *code に格納され，消費したバイト数ぶん進めたポインタを返す
//------------------------------------------------------------------------------
char *GraphicsPI::getCharCodeAt(char *p, uint16_t& code)
{
     if( *p == 0 )
     {
          code = 0x0000;
          return p;
     }
     if( (*p & 0xF0) == 0xE0 )
     {
          code = (((uint16_t)(*p & 0x0F))<<12) | (((uint16_t)(*(p+1) & 0x3F))<<6) | ((uint16_t)(*(p+2) & 0x3F));
          return p+3;
     }
     if( (*p & 0xE0) == 0xC0 )
     {
          code = (((uint16_t)(*p & 0x1F))<<6) | ((uint16_t)(*(p+1) & 0x3F));
          return p+2;
     }
     else if( *p <= 0x7E )
     {
          code = (uint16_t)*p;
          return p+1;
     }
     code = 0x0000;
     return p+1;
}

//------------------------------------------------------------------------------
int16_t GraphicsPI::drawText(int16_t x, int16_t y, const char *str, /*uint8_t size,*/ uint16_t color)
{
     char *p = const_cast<char *>(str);
     uint16_t code;
     while( *p )
     {
          p = getCharCodeAt(p, code);
          x = drawChar(x, y, code, color);
     }
     return x;
}

//------------------------------------------------------------------------------
int16_t GraphicsPI::getTextWidth(const char *str) //, uint8_t size)
{
     int16_t w = 0;
     char *p = const_cast<char *>(str);
     std::map<uint16_t, Font>::iterator f;
     uint16_t code;
     while( *p )
     {
          p = getCharCodeAt(p, code);
          f = m_currentFont->find(code);
          if( f != m_currentFont->end() )
          {
               w += f->second.width;
          }
     }
     return w;
}

//------------------------------------------------------------------------------
int16_t GraphicsPI::getTextHeight()
{
     return m_currentFont->begin()->second.height;
}

//------------------------------------------------------------------------------
void GraphicsPI::drawText(Rect& r, const char *str, uint8_t align, uint16_t fgcol)
{
     int16_t x = r.left, y = r.top;
     int16_t w = getTextWidth(str);
     int16_t h = getTextHeight();
     if( align & ALIGN_CENTER )
     {
          x = r.left + (r.width - w)/2;
     }
     else if( align & ALIGN_RIGHT )
     {
          x = r.bottomRight().x - w;
     }
     if( align & ALIGN_MIDDLE )
     {
          y = r.top + (r.height - h)/2;
     }
     else if( align & ALIGN_BOTTOM )
     {
          y = r.bottomRight().y - h;
     }

     char *p = const_cast<char *>(str);
     uint16_t code;
     while( *p )
     {
          p = getCharCodeAt(p, code);
          Font& font = (*m_currentFont)[code];
          if( r.include(x, y) && r.include(x+font.width-1, y+font.height-1) )
          {
               x = drawChar(x, y, code, fgcol);
          }
          else
          {
               x += font.width;
          }
     }
     // drawText(x, y, str, fgcol);   //size, fgcol);
}

//------------------------------------------------------------------------------
void GraphicsPI::drawText(Rect& r, const char *str, uint8_t align, uint16_t fgcol, uint16_t bkcol)
{
     fillRect(r, bkcol);
     drawText(r, str, align, fgcol);
}

//------------------------------------------------------------------------------
void GraphicsPI::drawImage(Rect& r, std::vector<uint16_t>& image)
{
     if( !m_available ){ return; }

     std::vector<uint16_t>::iterator it = image.begin();
     for( int16_t y = r.top ; y < r.top+r.height ; y++ )
     {
          uint32_t offset = offsetOfCoord(r.left, y);
          for( int16_t n = 0 ; n < r.width ; n++ )
          {
               m_fbp[offset+n] = *it++;
          }
     }
}

//------------------------------------------------------------------------------
void GraphicsPI::getImage(Rect& r, std::vector<uint16_t>& image)
{
     if( !m_available ){ return; }

     image.clear();
     for( int16_t y = r.top ; y < r.top+r.height ; y++ )
     {
          uint32_t offset = offsetOfCoord(r.left, y);
          for( int16_t n = 0 ; n < r.width ; n++ )
          {
               image.push_back(m_fbp[offset+n]);
          }
     }
}
