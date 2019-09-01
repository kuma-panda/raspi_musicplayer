#ifndef PNG_IMAGE_H
#define PNG_IMAGE_H

#include <vector>
#include <cstdint>

class PNGImage
{
    private:
        static const int HEADER_SIZE;
        int m_width;
        int m_height;
        std::vector<uint16_t> m_data;
    public:
        PNGImage();
        void read(const char *path);
        int getWidth(){ return m_width; }
        int getHeight(){ return m_height; }
        uint16_t getPixel(int x, int y){
            if( m_width == 0 || m_height == 0 ){ return 0x0000; }
            return m_data[m_width*y+x];
        }
};

#endif
