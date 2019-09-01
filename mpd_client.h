#ifndef CLIENTSOCK_H
#define CLIENTSOCK_H

#include <iostream>
#include <thread>
#include <mutex>
#include <string>
#include <cstdint>
#include <vector>
#include <deque>
#include <algorithm>

#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/signal.h>

#include "png_image.h"
#include "picojson.h"

//------------------------------------------------------------------------------
class TCPClient 
{
    private:
        static const int  BUFFER_SIZE;
        static const char DELIMITOR;

        std::string             m_host;
        uint32_t                m_port;
        bool                    m_connected;
        int                     m_sockfd;
        std::vector<uint8_t>    m_txBuffer;
        std::deque<std::string> m_rxBuffer;

        struct sockaddr_in m_servaddr;
        struct hostent    *m_server;

        bool m_terminated;
        std::thread *m_thread;
        std::mutex   m_mutex;

        void connect();
        void execute();
        bool enableKeepalive();
        void internalSend();
        void internalReceive();
        void addRxLines(std::string buffer);
    public:
        TCPClient(const char *host, uint32_t port);
        ~TCPClient();

        bool hadError();
        void sendRawBytes(const void *data, uint32_t len);
        std::string receive();
};

//------------------------------------------------------------------------------
struct PlayerStatus
{
    enum {
        PLAYERSTATE_STOP  = 0,
        PLAYERSTATE_PAUSE = 1,
        PLAYERSTATE_PLAY  = 2
    };
    long        volume;      // 0〜100
    int         state;       // PLAYERSTATE_xxxx
    int         song;        // 0〜
    int         elapsed;     // 秒単位

    PlayerStatus() : volume(50), state(PLAYERSTATE_STOP), song(0), elapsed(0){}
    void parseStatusResponse(const std::string& res);
    bool playing(){ return state != PLAYERSTATE_STOP; }
    PlayerStatus clone(){
        PlayerStatus s;
        s.volume  = this->volume;
        s.state   = this->state;
        s.song    = this->song;
        s.elapsed = this->elapsed;
        return s;
    }
};

//------------------------------------------------------------------------------
class MPDClient
{
    private:
        static const char *SERVER_ADDR;
        static const int   SERVER_PORT;
        static const char *BEGIN_COMMAND_LIST;
        static const char *END_COMMAND_LIST;

        enum {
            STATE_WAIT_CONNECTION,
            STATE_READY,
            STATE_WAIT_RESPONSE
        };

        TCPClient              *m_tcpClient;
        std::deque<std::string> m_txBuffer;
        std::deque<std::string> m_rxBuffer;
        int                     m_state;
        PlayerStatus            m_playerStatus;
        bool                    m_terminated;
        std::thread            *m_thread;
        std::mutex              m_mutex;

        void doReceive();
        void doSend();
        void update();
        void terminate();

    public:
        MPDClient();
        ~MPDClient();
        void addPlaylist(std::vector<std::string>& songs);
        void play(int song = 0);
        void togglePause();
        void next();
        void previous();
        void stop();
        void setVolume(long value);
        PlayerStatus getStatus();
};

//------------------------------------------------------------------------------
class Album;
class Song
{
    private:
        std::string m_title;        // 曲名
        uint16_t    m_trackIndex;   // トラックNo. (1がアルバムの先頭の曲)
        uint16_t    m_duration;     // 曲の演奏時間(秒単位)
        std::string m_filename;     // ファイル名
        Album      *m_album;        // この曲が収録されているアルバム
    public:
        Song(Album *album);
        Album *getAlbum(){ return m_album; }
        void loadFromJSON(picojson::object& obj);
        std::string getPath();
        std::string getTitle(){ return m_title; }
        uint16_t getDuration(){ return m_duration; }
        uint16_t getTrackIndex(){ return m_trackIndex; }
};

//------------------------------------------------------------------------------
class Artist;
class Album
{
    private:
        uint16_t            m_id;           // アルバムID
        std::vector<Song *> m_songs;        // アルバムに収録されている曲のリスト
        std::string         m_title;        // アルバムタイトル
        uint16_t            m_totalTime;    // 総演奏時間（各曲の演奏時間の総和、秒単位）
        uint16_t            m_year;         // アルバムの発売年（西暦）
        std::string         m_directory;    // フォルダ名（"trespass" など。フルパスではなくそのアルバムの曲が格納されたディレクトリ名であることに注意）
        Artist             *m_artist;       // このアルバムを所有するアーティスト
        PNGImage            m_image;        // カバーアート画像データ   

    public:
        Album(Artist *artist);
        ~Album();
        Artist *getArtist(){ return m_artist; }
        void loadFromJSON(picojson::object& obj);
        void loadCoverImage();
        uint16_t getID(){ return m_id; }
        std::string getTitle(){ return m_title; }
        std::string getDirectory(){ return m_directory; }
        uint16_t getNumTracks(){ return (uint16_t)m_songs.size(); }
        uint16_t getTotalTime(){ return m_totalTime; }
        uint16_t getYear(){ return m_year; }
        Song *getSong(int index){ return m_songs[index]; }
        PNGImage *getCoverImage(){ return &m_image; }
        std::string getPath();
};

//------------------------------------------------------------------------------
class Artist
{
    private:
        uint16_t             m_id;          // アーティストID
        std::string          m_directory;   // ディレクトリ名
        std::vector<Album *> m_albums;      // アルバムのリスト
        std::string          m_name;        // アーティスト名

    public:
        Artist();
        ~Artist();
        void loadFromJSON(picojson::object& obj);
        uint16_t getID(){ return m_id; }
        std::string getName(){ return m_name; }
        std::string getDirectory(){ return m_directory; }
        uint16_t getNumAlbums(){ return (uint16_t)m_albums.size(); }
        Album *getAlbum(int index){ return m_albums[index]; }
        std::string getPath();
};

//------------------------------------------------------------------------------
class ArtistList
{
    private:
        std::vector<Artist *> m_artists;

    public:
        ArtistList();
        ~ArtistList();
        void loadFromJSON(const char *path);
        uint16_t getNumArtists(){ return (uint16_t)m_artists.size(); }
        Artist *getArtistOfIndex(int index);
        Artist *getArtistByID(uint16_t artistID);
};

#endif
