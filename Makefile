music_player: mpd_client.o png_image.o
	g++ -o music_player mpd_client.o png_image.o -lpthread -lpng16
mpd_client.o: mpd_client.cpp mpd_client.h png_image.h
	g++ -c mpd_client.cpp
png_image.o: png_image.cpp png_image.h
	g++ -c png_image.cpp
clean:; rm -f *.o *~ music_player