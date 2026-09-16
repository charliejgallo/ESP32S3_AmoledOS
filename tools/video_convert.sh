#!/bin/zsh
#
# Converts a video into what the Video app plays: an AVI of MJPEG frames at
# the screen's exact size, and beside it the sound as a 16-bit PCM WAV that
# the firmware's player already knows how to play.
#
#   ./tools/video_convert.sh clip.mp4              # clip.avi + clip.wav next to it
#   ./tools/video_convert.sh clip.mp4 12 5 out/    # fps, JPEG quality (2 best .. 31), folder
#
# Then upload both files to the card's videos folder from the portal, or:
#
#   curl -X POST "http://<board>/api/upload?dir=videos&name=clip.avi" --data-binary @clip.avi
#   curl -X POST "http://<board>/api/upload?dir=videos&name=clip.wav" --data-binary @clip.wav
#
# Why 12 fps by default: the card reads at about 470 KB/s and the decoder
# takes 20-30 ms per frame, one after the other in the worker (docs/VIDEO.md);
# at 15 fps a 12 KB frame just fits and a 27 KB one does not, and the player
# then skips one frame in three. At 12 fps both play every frame.
#
# Why two files: the AVI carries only the pictures because the app hands
# the sound to aos_hal_player_play(), which takes a path to a WAV, and uses
# its position as the clock the frames follow. Why MJPEG: every frame is
# its own JPEG, so a late frame is skipped with a seek and the picture never
# smears, and the board decodes them with esp_new_jpeg (docs/VIDEO.md has
# the numbers). Why 368x448: the decoder writes straight into the screen
# buffer, no scaling on the watch.
#
set -e

IN=${1:?usage: video_convert.sh <input> [fps=12] [quality=5] [out_dir]}
FPS=${2:-12}
Q=${3:-5}
OUT=${4:-$(dirname "$IN")}
NAME=$(basename "${IN%.*}")
W=368
H=448

command -v ffmpeg > /dev/null || { echo "ffmpeg is missing (brew install ffmpeg)"; exit 1; }
mkdir -p "$OUT"

# The picture: cover the screen (scale up to fill, then crop the excess),
# fixed frame rate, JPEG 4:2:0 which is the cheapest to decode.
ffmpeg -v error -y -i "$IN" \
    -vf "scale=$W:$H:force_original_aspect_ratio=increase,crop=$W:$H,fps=$FPS" \
    -pix_fmt yuvj420p -c:v mjpeg -q:v "$Q" -an \
    "$OUT/$NAME.avi"

# The sound, only if the input has any: mono 16 kHz, which the ES8311 plays
# and which costs 32 KB/s of card.
if ffprobe -v error -select_streams a:0 -show_entries stream=codec_type -of csv=p=0 "$IN" 2>/dev/null | grep -q audio; then
    ffmpeg -v error -y -i "$IN" -vn -ac 1 -ar 16000 -c:a pcm_s16le "$OUT/$NAME.wav"
    has_audio=yes
else
    rm -f "$OUT/$NAME.wav"
    has_audio=no
fi

frames=$(ffprobe -v error -count_packets -select_streams v:0 -show_entries stream=nb_read_packets -of csv=p=0 "$OUT/$NAME.avi")
bytes=$(wc -c < "$OUT/$NAME.avi" | tr -d ' ')
echo "$OUT/$NAME.avi: $frames frames at $FPS fps, ${W}x${H}, $((bytes / 1024)) KB ($((bytes / frames / 1024)) KB per frame, $((bytes * FPS / frames / 1024)) KB/s), audio: $has_audio"
