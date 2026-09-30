"""Cross-section of a lightning channel: hot core + glow + halo (additive).
Writes materials/hl2rpm/weather/lightning_glow.vtf and lightning_bolt.vmt into the mod."""
import os
import struct
import numpy as np

MOD = r"E:\Steam\steamapps\sourcemods\hl2rpm"
W, H = 128, 16

u = ( np.arange( W ) + 0.5 ) / W - 0.5
core = np.exp( -( u / 0.045 ) ** 2 )
glow = np.exp( -( u / 0.16 ) ** 2 ) * 0.38
halo = np.exp( -( u / 0.34 ) ** 2 ) * 0.10
edge = np.clip( ( 0.5 - np.abs( u ) ) / 0.08, 0, 1 )	# exactly 0 at the quad edges
prof = np.clip( ( core + glow + halo ) * edge, 0, 1 )

img = np.zeros( ( H, W, 4 ), np.float32 )
img[..., 0] = prof[None, :]
img[..., 1] = prof[None, :]
img[..., 2] = prof[None, :]
img[..., 3] = prof[None, :]


def mips( a ):
	out = [a]
	while out[-1].shape[0] > 1 or out[-1].shape[1] > 1:
		p = out[-1]
		h = max( 1, p.shape[0] // 2 )
		w = max( 1, p.shape[1] // 2 )
		if p.shape[0] > 1:
			p = 0.5 * ( p[0::2] + p[1::2] )
		if p.shape[1] > 1:
			p = 0.5 * ( p[:, 0::2] + p[:, 1::2] )
		out.append( p.reshape( h, w, 4 ) )
	return out


levels = mips( img )
flags = 0x4 | 0x8 | 0x2000	# clamp s/t, 8-bit alpha
header = struct.pack( "<4sIIIHHIHH4x3f4xfIBiBBH",
	b"VTF\0", 7, 2, 80, W, H, flags, 1, 0,
	0.5, 0.5, 0.5, 1.0,
	12, len( levels ), -1, 0, 0, 1 ).ljust( 80, b"\0" )

data = bytearray()
for lv in reversed( levels ):
	b = np.clip( lv * 255 + 0.5, 0, 255 ).astype( np.uint8 )
	bgra = b[..., [2, 1, 0, 3]]
	data += bgra.tobytes()

d = os.path.join( MOD, "materials", "hl2rpm", "weather" )
os.makedirs( d, exist_ok=True )
with open( os.path.join( d, "lightning_glow.vtf" ), "wb" ) as f:
	f.write( header + data )

with open( os.path.join( d, "lightning_bolt.vmt" ), "w", newline="\r\n" ) as f:
	f.write( '''"UnlitGeneric"
{
	// HL2RPM: lightning channel (weather_render.cpp, WeatherRender_Lightning)
	"$basetexture" "hl2rpm/weather/lightning_glow"
	"$additive" "1"
	"$vertexcolor" "1"
	"$vertexalpha" "1"
	"$nocull" "1"
	"$nofog" "1"
}
''' )
print( "mips", len( levels ), "bytes", len( data ) )
