#include "mpd_client.h"

#include <stdexcept>
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <chrono>

#include <strings.h>



//==============================================================================
//  TCPClient
//==============================================================================
const int  TCPClient::BUFFER_SIZE = 256;
const char TCPClient::DELIMITOR   = '\n';

//------------------------------------------------------------------------------
TCPClient::TCPClient(const char *host, uint32_t port) 
    : m_host(host), m_port(port), m_sockfd(-1), m_terminated(false)
{
    connect();
    m_thread = new std::thread([this](){ execute(); });
}

//------------------------------------------------------------------------------
TCPClient::~TCPClient()
{
    m_terminated = true;
    m_thread->join();
    delete m_thread;
    ::close(m_sockfd);
}

//------------------------------------------------------------------------------
void TCPClient::connect()
{
    ::bzero(&m_servaddr, sizeof(m_servaddr));
    m_servaddr.sin_family = AF_INET;
    m_server = gethostbyname(m_host.c_str());
    ::bcopy((char *)m_server->h_addr, (char *)&m_servaddr.sin_addr.s_addr, m_server->h_length);
    m_servaddr.sin_port = htons(m_port);

    m_sockfd = ::socket(AF_INET, SOCK_STREAM, 0);

    enableKeepalive();

    if( ::connect(m_sockfd, (struct sockaddr *)&m_servaddr, sizeof(m_servaddr)) < 0 )
    {
        perror("connect");
        throw std::runtime_error("Unable to connect MPD server");
    }
}

//------------------------------------------------------------------------------
bool TCPClient::hadError()
{
    int error = 0;
    socklen_t len = sizeof(error);
    int retval = ::getsockopt(m_sockfd, SOL_SOCKET, SO_ERROR, &error, &len);

    return (retval != 0 || error != 0);
}

//------------------------------------------------------------------------------
bool TCPClient::enableKeepalive() 
{
    int yes = 1;

    if( ::setsockopt(m_sockfd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(int)) == -1 )
    {
        return false;
    }

    int idle = 1;
    if( ::setsockopt(m_sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(int)) == -1 )
    {
        return false;
    }   

    int interval = 1;
    if( ::setsockopt(m_sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(int)) == -1 )
    {
        return false;
    }

    int maxpkt = 10;
    if( ::setsockopt(m_sockfd, IPPROTO_TCP, TCP_KEEPCNT, &maxpkt, sizeof(int)) == -1 )
    {
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------
void TCPClient::execute()
{
    while( !m_terminated )
    {
        internalSend();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        internalReceive();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

//------------------------------------------------------------------------------
void TCPClient::sendRawBytes(const void *data, uint32_t len)
{
    uint8_t *p = (uint8_t *)data;
    m_mutex.lock();
    for( uint32_t n = 0 ; n < len ; n++ )
    {
        m_txBuffer.push_back(p[n]);
    }
    m_mutex.unlock();
}

//------------------------------------------------------------------------------
void TCPClient::internalSend()
{    
    m_mutex.lock();

    if( m_txBuffer.empty() )
    {
        m_mutex.unlock();
        return;
    }

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 10000;

    fd_set mask;
    FD_ZERO(&mask);
    FD_SET(m_sockfd, &mask);

    int w = m_sockfd + 1;
    
    switch( ::select(w, NULL, &mask, NULL, &tv) )
    {
        case -1:
            if( errno != EINTR )
            {
                perror("select(recv)");
            }
            break;
        case 0:
            // timeout
            break;
        default:
            if( FD_ISSET(m_sockfd, &mask) ) 
            {
                int sentBytes = ::write(m_sockfd, &m_txBuffer[0], m_txBuffer.size());
                if( sentBytes < 0 )
                {
                    perror("write");
                    break;
                }
                if( sentBytes > 0 )
                {
                    auto s = m_txBuffer.begin();
                    auto e = m_txBuffer.begin();
                    std::advance(e, sentBytes);
                    m_txBuffer.erase(s, e);
                }
            }
            break;
    }

    m_mutex.unlock();
}

//------------------------------------------------------------------------------
std::string TCPClient::receive()
{
    std::string r;
    m_mutex.lock();
    if( !m_rxBuffer.empty() )
    {
        r = m_rxBuffer.front();
        m_rxBuffer.pop_front();
    }
    m_mutex.unlock();
    return r;
}

//------------------------------------------------------------------------------
void TCPClient::internalReceive()
{
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 10000;

    fd_set mask;
    FD_ZERO(&mask);
    FD_SET(m_sockfd, &mask);

    int w = m_sockfd + 1;

    switch( ::select(w, &mask, NULL, NULL, &tv) )
    {
        case -1:
            if( errno != EINTR )
            {
                perror("select(recv)");
            }
            break;
        case 0:
            // timeout
            break;
        default:
            if( FD_ISSET(m_sockfd, &mask) )
            {
                std::vector<uint8_t> rbuf(BUFFER_SIZE);
                int tn = ::read(m_sockfd, &rbuf[0], BUFFER_SIZE);
                if( tn > 0 )
                {
                    std::string lines(((char *)&rbuf[0]));
                    m_mutex.lock();
                    addRxLines(lines);
                    m_mutex.unlock();
                }
            }
            break;
    }
}

//------------------------------------------------------------------------------
//  １行単位で m_rxBuffer へ文字列を追加
//  （各文字列にはデリミタ\nが付く）
//------------------------------------------------------------------------------
void TCPClient::addRxLines(std::string buffer)
{
    auto offset = std::string::size_type(0);
    auto list = std::vector<std::string>();
    while( true ) 
    {
        auto pos = buffer.find(DELIMITOR, offset);
        if( pos == std::string::npos )
        {
            list.push_back(buffer.substr(offset));
            break;
        }
        list.push_back(buffer.substr(offset, pos - offset + 1));
        offset = pos + 1;
    }
    for( auto i = list.begin() ; i != list.end() ; i++ )
    {
        // m_rxBuffer の後端の文字列にデリミタが含まれているか確認する。
        // 含まれていない場合は前回の受信時に受信データが「泣き別れ」になっているので、
        // push_back ではなく、後端の要素に追加する必要がある。
        if( m_rxBuffer.empty() || m_rxBuffer.back().find(DELIMITOR) != std::string::npos )
        {
            m_rxBuffer.push_back(*i);
        }
        else
        {
            m_rxBuffer.back() += *i;
        }
    }
}



//==============================================================================
//   PlayerStatus
//==============================================================================
void PlayerStatus::parseStatusResponse(const std::string& str)
{
    if( str.find("song:") == 0 )
    {
        this->song = std::stoi(str.c_str()+6);
    }
    if( str.find("state:") == 0 )
    {
        if( str.find("stop") != std::string::npos )
        {
            this->state = PLAYERSTATE_STOP;
            this->elapsed = 0;
        }
        else if( str.find("pause") != std::string::npos )
        {
            this->state = PLAYERSTATE_PAUSE;
        }
        else if( str.find("play") != std::string::npos )
        {
            this->state = PLAYERSTATE_PLAY;
        }
    }
    if( str.find("elapsed:") == 0 )
    {
        this->elapsed = (int)std::atof(str.c_str()+9);
    }
}

//==============================================================================
//  MPDClient
//==============================================================================
const char *MPDClient::SERVER_ADDR = "raspberrypi.local";   //127.0.0.1";
const int   MPDClient::SERVER_PORT = 6600;
const char *MPDClient::BEGIN_COMMAND_LIST = "command_list_begin";
const char *MPDClient::END_COMMAND_LIST = "command_list_end";

//------------------------------------------------------------------------------
MPDClient::MPDClient() : m_state(STATE_WAIT_CONNECTION), m_terminated(false)
{
    m_tcpClient = new TCPClient(SERVER_ADDR, SERVER_PORT);
    m_thread = new std::thread([this](){ update(); });
}

//------------------------------------------------------------------------------
MPDClient::~MPDClient()
{
    terminate();
    delete m_tcpClient;
}

//------------------------------------------------------------------------------
PlayerStatus MPDClient::getStatus()
{
    m_mutex.lock();
    PlayerStatus s = m_playerStatus.clone();
    m_mutex.unlock();
    return s;
}

//------------------------------------------------------------------------------
void MPDClient::update()
{
    int idleState = 0;

    while( !m_terminated )
    {
        m_mutex.lock();

        switch( m_state )
        {
            case STATE_WAIT_CONNECTION:
                doReceive();
                if( m_rxBuffer.front().find("OK") != std::string::npos )
                {
                    m_state = STATE_READY;
                }
                break;
            case STATE_READY:
                if( !m_txBuffer.empty() )
                {
                    doSend();
                }
                else
                {
                    if( idleState == 0 )
                    {
                        m_tcpClient->sendRawBytes("status\n", 7);
                    }
                    else
                    {
                        m_tcpClient->sendRawBytes("currentsong\n", 12);
                    }
                    idleState = 1 - idleState;
                }
                m_state = STATE_WAIT_RESPONSE;
                break;
            case STATE_WAIT_RESPONSE:
                doReceive();
                while( !m_rxBuffer.empty() )
                {
                    std::string str = m_rxBuffer.front();
                    m_rxBuffer.pop_front();
                    m_playerStatus.parseStatusResponse(str);
                    if( str.find("OK") == 0 )
                    {
                        m_state = STATE_READY;
                    }
                }
                break;
        }

        m_mutex.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

//------------------------------------------------------------------------------
void MPDClient::doReceive()
{
    std::string str = m_tcpClient->receive();
    while( str.length() > 0 )
    {
        m_rxBuffer.push_back(str);
        str = m_tcpClient->receive();
    }
}

//------------------------------------------------------------------------------
void MPDClient::doSend()
{
    if( m_txBuffer.empty() )
    {
        return;
    }

    std::string cmd;
    if( m_txBuffer.front().find(BEGIN_COMMAND_LIST) != std::string::npos )
    {
        do
        {
            cmd = m_txBuffer.front();
            m_tcpClient->sendRawBytes((cmd+"\n").c_str(), cmd.length()+1);
            m_txBuffer.pop_front();
        }
        while( !m_txBuffer.empty() && (cmd.find(END_COMMAND_LIST) == std::string::npos) );
        m_state = STATE_WAIT_RESPONSE;
    }
    else
    {
        cmd = m_txBuffer.front();
        m_tcpClient->sendRawBytes((cmd+"\n").c_str(), cmd.length()+1);
        m_txBuffer.pop_front();
    }
}

//------------------------------------------------------------------------------
void MPDClient::addPlaylist(std::vector<std::string>& songs)
{
    m_mutex.lock();
    m_txBuffer.push_back(BEGIN_COMMAND_LIST);
    m_txBuffer.push_back("stop");
    m_txBuffer.push_back("clear");
    char buf[20];
    for( int i = 0 ; i < (int)songs.size() ; i++ )
    {
        std::stringstream ss;
        ss << "add " << songs[i] << ".mp3";
        m_txBuffer.push_back(ss.str());
    }
    m_txBuffer.push_back(END_COMMAND_LIST);
    m_mutex.unlock();
}

//------------------------------------------------------------------------------
void MPDClient::play(int song)
{
    m_mutex.lock();
    std::stringstream ss;
    ss << "play " << song;  // song は 0 が先頭の曲になる
    m_txBuffer.push_back(ss.str());
    m_mutex.unlock();
}

//------------------------------------------------------------------------------
void MPDClient::togglePause()
{
    m_mutex.lock();
    if( m_playerStatus.state == PlayerStatus::PLAYERSTATE_PAUSE )
    {
        m_txBuffer.push_back("play");
    }
    else if( m_playerStatus.state == PlayerStatus::PLAYERSTATE_PLAY )
    {
        m_txBuffer.push_back("pause");
    }
    m_mutex.unlock();
}

//------------------------------------------------------------------------------
void MPDClient::next()
{
    m_mutex.lock();
    m_txBuffer.push_back("next");
    m_mutex.unlock();
}

//------------------------------------------------------------------------------
void MPDClient::previous()
{
    m_mutex.lock();
    m_txBuffer.push_back("previous");
    m_mutex.unlock();
}

//------------------------------------------------------------------------------
void MPDClient::stop()
{
    m_mutex.lock();
    m_txBuffer.push_back("stop");
    m_mutex.unlock();
}

//------------------------------------------------------------------------------
void MPDClient::setVolume(long value)
{
    m_mutex.lock();
    std::stringstream ss;
    ss << "volume " << value;
    m_txBuffer.push_back(ss.str());
    m_mutex.unlock();
}

//------------------------------------------------------------------------------
void MPDClient::terminate()
{
    stop();
    bool b;
    do
    {
        m_mutex.lock();
        b = !m_txBuffer.empty();
        m_mutex.unlock();
    }
    while( b );
    
    m_terminated = true;
    m_thread->join();
}



//==============================================================================
//  Song
//==============================================================================
Song::Song(Album *album) : m_album(album), m_trackIndex(0), m_duration(0)
{

}

//------------------------------------------------------------------------------
void Song::loadFromJSON(picojson::object& obj)
{
    m_trackIndex = (uint16_t)obj["index"].get<double>();
    m_title = obj["title"].get<std::string>();
    m_duration = (uint16_t)obj["duration"].get<double>();
    m_filename = obj["filename"].get<std::string>();
}

//------------------------------------------------------------------------------
std::string Song::getPath()
{
    return m_album->getPath() + "/" + m_filename;
}



//==============================================================================
//  Album
//==============================================================================
Album::Album(Artist *artist) : m_artist(artist), m_id(0),
    m_totalTime(0), m_year(0)
{

}

//------------------------------------------------------------------------------
Album::~Album()
{
    for( auto i = m_songs.begin() ; i != m_songs.end() ; i++ )
    {
        delete *i;
    }
}

//------------------------------------------------------------------------------
void Album::loadFromJSON(picojson::object& obj)
{
    m_id = (uint16_t)obj["id"].get<double>();
    m_title = obj["title"].get<std::string>();
    m_year = (uint16_t)obj["year"].get<double>();
    m_directory = obj["directory"].get<std::string>();
    m_totalTime = (uint16_t)obj["totalTime"].get<double>();
    picojson::array& collection = obj["tracks"].get<picojson::array>();
    for( int n = 0 ; n < (int)collection.size() ; n++ )
    {
        picojson::object& obj = collection[n].get<picojson::object>();
        Song *song = new Song(this);
        song->loadFromJSON(obj);
        m_songs.push_back(song);
    }
}

//------------------------------------------------------------------------------
void Album::loadCoverImage()
{
    std::stringstream ss;
    ss << "/mnt/music/" << getPath() << "/coverart.png";
    m_image.read(ss.str().c_str());
}

//------------------------------------------------------------------------------
std::string Album::getPath()
{
    return m_artist->getPath() + "/" + m_directory;
}



//==============================================================================
//  Artist
//==============================================================================
Artist::Artist() : m_id(0)
{
}

//------------------------------------------------------------------------------
Artist::~Artist()
{
    for( auto i = m_albums.begin() ; i != m_albums.end() ; i++ )
    {
        delete *i;
    }
}

//------------------------------------------------------------------------------
void Artist::loadFromJSON(picojson::object& obj)
{
    m_id = (uint16_t)obj["id"].get<double>();
    m_name = obj["name"].get<std::string>();
    m_directory = obj["directory"].get<std::string>();
    picojson::array& collection = obj["albums"].get<picojson::array>();
    for( int n = 0 ; n < (int)collection.size() ; n++ )
    {
        picojson::object& obj = collection[n].get<picojson::object>();
        Album *album = new Album(this);
        album->loadFromJSON(obj);
        m_albums.push_back(album);
    }
}

//------------------------------------------------------------------------------
std::string Artist::getPath()
{
    std::stringstream ss;
    ss << "usb/" << m_directory;
    return ss.str();
}



//==============================================================================
//  ArtistList
//==============================================================================
ArtistList::ArtistList()
{

}

//------------------------------------------------------------------------------
ArtistList::~ArtistList()
{
    for( auto i = m_artists.begin() ; i != m_artists.end() ; i++ )
    {
        delete *i;
    }
}

//------------------------------------------------------------------------------
void ArtistList::loadFromJSON(const char *path)
{
    std::ifstream ifs(path, std::ios::in);
    if( ifs.fail() ) 
    {
        throw std::runtime_error("Unable to load database file");
    }
    const std::string json((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();

    picojson::value v;
    const std::string err = picojson::parse(v, json);
    if( !err.empty() )
    {
        throw std::runtime_error(err);
    }

    picojson::array& artists = v.get<picojson::array>();
    for( int n = 0 ; n < (int)artists.size() ; n++ )
    {
        picojson::object& obj = artists[n].get<picojson::object>();
        Artist *artist = new Artist();
        artist->loadFromJSON(obj);
        m_artists.push_back(artist);
    }
}

//------------------------------------------------------------------------------
Artist *ArtistList::getArtistOfIndex(int index)
{
    return m_artists[index];
}

//------------------------------------------------------------------------------
Artist *ArtistList::getArtistByID(uint16_t id)
{
    for( auto i = m_artists.begin() ; i != m_artists.end() ; i++ )
    {
        if( (*i)->getID() == id )
        {
            return *i;
        }
    }
    throw std::runtime_error("Undefined Artist ID");
}

//------------------------------------------------------------------------------
int main()
{
    ArtistList artists;
    artists.loadFromJSON("/mnt/music/usb/database.json");
    int num = artists.getNumArtists();
    for( int n = 0 ; n < num ; n++ )
    {
        Artist *artist = artists.getArtistOfIndex(n);
        std::cout << artist->getName() << std::endl;
    }
    return 0;
}
