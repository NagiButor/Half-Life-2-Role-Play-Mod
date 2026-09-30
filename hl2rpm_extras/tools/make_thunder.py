"""Converts the user's lightning / thunder mp3s into the mod's thunder set.

close_*.wav  - strikes within ~1.2 km (crack + boom), trimmed to the onset
mid_*.wav    - rumbles 1.2..4 km (long rolls, segments of the long recordings)
far_*.wav    - distant rolls (low-passed)

44.1 kHz 16-bit PCM, peak-normalized; the code sets the volume by distance.
"""
import os
import subprocess
import numpy as np

FF = r"E:\Programs\ffmpeg-2026-01-29-git-c898ddb8fe-full_build\bin\ffmpeg.exe"
SRC = r"E:\SourceModding\ProceduralWeatherAndSkyPromt\lighting sounds"
OUT = r"E:\Steam\steamapps\sourcemods\hl2rpm\sound\hl2rpm\weather\thunder"
SR = 44100

os.makedirs( OUT, exist_ok=True )
for f in os.listdir( OUT ):
	if f.endswith( ".wav" ):
		os.remove( os.path.join( OUT, f ) )


def load( name ):
	"""float32 [n, ch] at 44.1 kHz"""
	p = os.path.join( SRC, name )
	ch = int( subprocess.check_output( [ FF.replace( "ffmpeg.exe", "ffprobe.exe" ), "-v", "error", "-select_streams", "a:0",
		"-show_entries", "stream=channels", "-of", "csv=p=0", p ] ).decode().strip().split( "," )[0] )
	raw = subprocess.check_output( [ FF, "-v", "error", "-i", p, "-ar", str( SR ), "-f", "f32le", "-" ] )
	x = np.frombuffer( raw, np.float32 ).reshape( -1, ch )
	return x


def envelope_db( x, hop_s=0.05 ):
	m = x.mean( axis=1 )
	hop = int( SR * hop_s )
	n = len( m ) // hop
	rms = np.sqrt( ( m[:n * hop].reshape( n, hop ) ** 2 ).mean( 1 ) + 1e-12 )
	return 20 * np.log10( rms + 1e-9 ), hop


def trim_onset( x, rel_db=-35.0, pre_s=0.02 ):
	db, hop = envelope_db( x, 0.01 )
	i = int( np.argmax( db > db.max() + rel_db ) )
	start = max( 0, i * hop - int( pre_s * SR ) )
	return x[start:]


def segments( x, rel_db=-30.0, min_gap_s=1.2, min_len_s=5.0 ):
	db, hop = envelope_db( x, 0.05 )
	on = db > db.max() + rel_db
	segs = []
	i = 0
	n = len( on )
	while i < n:
		if not on[i]:
			i += 1
			continue
		j = i
		last_on = i
		while j < n and ( on[j] or ( j - last_on ) * 0.05 < min_gap_s ):
			if on[j]:
				last_on = j
			j += 1
		if ( last_on - i ) * 0.05 >= min_len_s:
			segs.append( ( max( 0, i * hop - int( 0.3 * SR ) ), min( len( x ), ( last_on + 1 ) * hop + int( 1.0 * SR ) ) ) )
		i = j
	return segs


def fade( x, fin_s=0.01, fout_s=0.8 ):
	x = x.copy()
	a = int( fin_s * SR )
	b = int( fout_s * SR )
	if a > 0:
		x[:a] *= np.linspace( 0, 1, a )[:, None]
	if b > 0 and b < len( x ):
		x[-b:] *= np.linspace( 1, 0, b )[:, None] ** 2
	return x


def lowpass( x, cutoff ):
	# one-pole x2 (12 dB/oct) - enough to push a roll into the distance
	a = np.exp( -2 * np.pi * cutoff / SR )
	y = x.copy()
	for _ in range( 2 ):
		out = np.empty_like( y )
		prev = np.zeros( y.shape[1], np.float32 )
		for i in range( len( y ) ):
			prev = ( 1 - a ) * y[i] + a * prev
			out[i] = prev
		y = out
	return y


def lowpass_fast( x, cutoff ):
	# FFT brick-ish low-pass with a smooth knee (fast for long files)
	n = len( x )
	X = np.fft.rfft( x, axis=0 )
	f = np.fft.rfftfreq( n, 1.0 / SR )
	g = 1.0 / np.sqrt( 1.0 + ( f / cutoff ) ** 4 )
	return np.fft.irfft( X * g[:, None], n=n, axis=0 ).astype( np.float32 )


def save( name, x, peak_db, rate=SR ):
	if rate != SR:
		# already low-passed well below the new Nyquist: plain decimation by 2
		assert SR % rate == 0
		k = SR // rate
		x = x[: len( x ) // k * k].reshape( -1, k, x.shape[1] ).mean( axis=1 )
	peak = np.abs( x ).max() + 1e-9
	x = x * ( 10 ** ( peak_db / 20.0 ) / peak )
	pcm = np.clip( x * 32767.0, -32768, 32767 ).astype( "<i2" )
	ch = pcm.shape[1]
	data = pcm.tobytes()
	import struct
	hdr = b"RIFF" + struct.pack( "<I", 36 + len( data ) ) + b"WAVE"
	hdr += b"fmt " + struct.pack( "<IHHIIHH", 16, 1, ch, rate, rate * ch * 2, ch * 2, 16 )
	hdr += b"data" + struct.pack( "<I", len( data ) )
	with open( os.path.join( OUT, name ), "wb" ) as fp:
		fp.write( hdr + data )
	print( "%-14s %5.1fs %d ch %d Hz" % ( name, len( x ) / rate, ch, rate ) )


# ---------------- close strikes
close = [
	"34c5970d7bc80e6.mp3",						# short single strike
	"b6beb146d35201f.mp3",						# clean strike
	"jg-032316-sfx-lightning-crashing-2.mp3",	# summer lightning crash
	"lightning-bolt-thunder-crack_mkdwo3eo.mp3",	# thunder crack after a strong flash
	"lightning-bolt-thunder-crack_zkeioh4o.mp3",	# flash + long roll
	"isolated-rumbling-thunder_myts4rvd.mp3",	# strong discharge + roll
]
for i, name in enumerate( close ):
	x = trim_onset( load( name ) )
	save( "close_%02d.wav" % ( i + 1 ), fade( x, 0.003, 1.0 ), -1.0 )

# ---------------- mid rolls
mid_idx = 1
x = trim_onset( load( "4cebca2aa9d4d46.mp3" ) )
save( "mid_%02d.wav" % mid_idx, fade( lowpass_fast( x, 5000 ), 0.05, 2.0 ), -2.0, 22050 )
mid_idx += 1

storm = load( "thunderstorm-with-light-rain_m1bzbh4u.mp3" )
for a, b in segments( storm ):
	seg = storm[a:b]
	save( "mid_%02d.wav" % mid_idx, fade( lowpass_fast( seg, 5000 ), 0.3, 1.5 ), -2.0, 22050 )
	mid_idx += 1

# ---------------- far rolls
far_idx = 1
rolls = load( "several-rolling-thunders_gjc-sh4u.mp3" )
for a, b in segments( rolls ):
	seg = rolls[a:b]
	save( "far_%02d.wav" % far_idx, fade( lowpass_fast( seg, 1400 ), 0.4, 2.0 ), -3.0, 22050 )
	far_idx += 1
