all: scolorq.a

clean:
	rm scolorq.a spatial_color_quant.o

scolorq.a: spatial_color_quant.o
	$(AR) rcs $@ $<

spatial_color_quant.o: spatial_color_quant.cpp
	g++ -Wall -pedantic -O3 -c spatial_color_quant.cpp -o spatial_color_quant.o
