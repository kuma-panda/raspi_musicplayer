#include "png_image.h"
#include <string>
#include <iostream>
#include <stdexcept>
#include <stdio.h>
#include <png.h>

// libpng-config --cflags
// libpng-config --ldflags

const int PNGImage::HEADER_SIZE = 8;

//------------------------------------------------------------------------------
PNGImage::PNGImage() : m_width(0), m_height(0)
{

}

//------------------------------------------------------------------------------
void PNGImage::read(const char* path)
{
    FILE *fp = ::fopen(path, "rb");
    if( fp == NULL )
    {
        std::string msg = "Unable to open ";
        msg += path;
        std::cerr << msg << std::endl;
        return;
    }

    uint8_t header[HEADER_SIZE];
    uint32_t readSize = ::fread(header, 1, HEADER_SIZE, fp);
    if( ::png_sig_cmp(header, 0, HEADER_SIZE) )
    {
        std::string msg = "Failed in png_sig_cmp for ";
        msg += path;
        throw std::runtime_error(msg.c_str());
    }

    png_structp png = ::png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if( png == NULL )
    {
        std::string msg = "Failed in png_create_read_struct for ";
        msg += path;
        throw std::runtime_error(msg.c_str());
    }

    png_infop info = ::png_create_info_struct(png);
    if( info == NULL )
    {
        std::string msg = "Failed in png_create_info_struct for ";
        msg += path;
        throw std::runtime_error(msg.c_str());
    }

    ::png_init_io(png, fp);
    ::png_set_sig_bytes(png, readSize);
    ::png_read_png(png, info, PNG_TRANSFORM_PACKING|PNG_TRANSFORM_STRIP_16, NULL);

    m_width = ::png_get_image_width(png, info);
    m_height = ::png_get_image_height(png, info);

    png_bytepp datap = ::png_get_rows(png, info);

    png_byte type = ::png_get_color_type(png, info);
    if( type != PNG_COLOR_TYPE_RGB )
    {
        std::string msg = "Type mismatch of ";
        msg += path;
        throw std::runtime_error(msg.c_str());
    }

    m_data.resize(m_width * m_height);

    for( int y = 0; y < m_height ; y++ )
    {
        for( int x = 0 ; x < m_width ; x++ )
        {
            uint16_t red   = (uint16_t)*(datap[y]+3*x);
            uint16_t green = (uint16_t)*(datap[y]+3*x+1);
            uint16_t blue  = (uint16_t)*(datap[y]+3*x+2);
            if( x == 2 && y == 30 )
            {
                std::cout << red << ", " << green << ", " << blue << std::endl; 
            }
            m_data[y*m_width+x] = ((red << 8) & 0xF800) + ((green << 3) & 0x07E0) + (blue >> 3);
        }
    }
    ::png_destroy_read_struct(&png, &info, NULL);
    ::fclose(fp);
}

//------------------------------------------------------------------------------
// int main()
// {
//     PNGImage img;
//     img.read("./coverart.png");
//     std::cout << "width  : " << img.getWidth() << std::endl;
//     std::cout << "height : " << img.getHeight() << std::endl;
//     uint16_t p = img.getPixel(2, 30);
//     std::cout << "Color565 value of (2, 30) : " << p << std::endl;    
//     return 0;
// }
