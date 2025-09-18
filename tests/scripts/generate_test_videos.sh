#!/bin/bash
# Generate test videos for ASCII-Chat testing
# Creates a simulated webcam video of a person at their computer

set -e

# Script is in tests/scripts, fixtures are in tests/fixtures
SCRIPT_DIR="$(dirname "$0")"
FIXTURES_DIR="$SCRIPT_DIR/../fixtures"
mkdir -p "$FIXTURES_DIR"
cd "$FIXTURES_DIR"

echo "Generating test video for ASCII-Chat..."

# Generate a realistic webcam test video
# This simulates a person sitting at their computer with natural movements
generate_webcam_simulation() {
    echo "Creating simulated webcam video..."

    # Use FFmpeg to create a video that simulates a person at their desk
    # We'll use color patterns and shapes to simulate:
    # - A face (oval in upper center)
    # - Shoulders (wider shape below)
    # - Background (office/room)
    # - Natural lighting changes
    # - Slight movement to simulate breathing/shifting

    ffmpeg -f lavfi -i "
        nullsrc=size=640x480:rate=30:duration=10,
        format=rgb24,
        geq=r='
            /* Background - grayish office wall */
            128 + 20*sin(X/100) + 10*sin(Y/100) +

            /* Face - skin tone oval in center upper */
            if(pow((X-320)/80,2)+pow((Y-140)/100,2)<1,
                /* Skin tone with slight variation */
                100 + 80*exp(-pow((X-320)/80,2)-pow((Y-140)/100,2)) + 10*sin(T*2+X/50),
                0) +

            /* Eyes - two dark spots */
            if(pow((X-290)/15,2)+pow((Y-130)/10,2)<1, -50, 0) +
            if(pow((X-350)/15,2)+pow((Y-130)/10,2)<1, -50, 0) +

            /* Mouth - horizontal dark line */
            if(abs(X-320)<30*abs(Y-180)<5, -30, 0) +

            /* Shoulders - wider shape below face */
            if(Y>240,
                if(abs(X-320)<150-pow((Y-240)/50,2)*50,
                    50 + 30*sin(T*1.5), 0), 0) +

            /* Lighting variation over time */
            5*sin(T*0.5) + 3*cos(T*0.3)
        ':
        g='
            /* Similar pattern but slightly different for green channel */
            120 + 20*sin(X/100) + 10*sin(Y/100) +

            /* Face */
            if(pow((X-320)/80,2)+pow((Y-140)/100,2)<1,
                80 + 60*exp(-pow((X-320)/80,2)-pow((Y-140)/100,2)) + 10*sin(T*2+X/50),
                0) +

            /* Eyes */
            if(pow((X-290)/15,2)+pow((Y-130)/10,2)<1, -40, 0) +
            if(pow((X-350)/15,2)+pow((Y-130)/10,2)<1, -40, 0) +

            /* Mouth */
            if(abs(X-320)<30*abs(Y-180)<5, -25, 0) +

            /* Shoulders */
            if(Y>240,
                if(abs(X-320)<150-pow((Y-240)/50,2)*50,
                    40 + 30*sin(T*1.5), 0), 0) +

            5*sin(T*0.5) + 3*cos(T*0.3)
        ':
        b='
            /* Blue channel - less in skin tones */
            140 + 20*sin(X/100) + 10*sin(Y/100) +

            /* Face - less blue for skin tone */
            if(pow((X-320)/80,2)+pow((Y-140)/100,2)<1,
                40 + 30*exp(-pow((X-320)/80,2)-pow((Y-140)/100,2)) + 10*sin(T*2+X/50),
                0) +

            /* Eyes */
            if(pow((X-290)/15,2)+pow((Y-130)/10,2)<1, -30, 0) +
            if(pow((X-350)/15,2)+pow((Y-130)/10,2)<1, -30, 0) +

            /* Mouth */
            if(abs(X-320)<30*abs(Y-180)<5, -20, 0) +

            /* Shoulders - clothing color */
            if(Y>240,
                if(abs(X-320)<150-pow((Y-240)/50,2)*50,
                    80 + 30*sin(T*1.5), 0), 0) +

            5*sin(T*0.5) + 3*cos(T*0.3)
        ',

        /* Add some noise and movement */
        noise=alls=20:allf=t,

        /* Slight zoom/shift to simulate natural movement */
        zoompan=z='1.01+0.01*sin(2*PI*in/300)':
                x='iw/2-(iw/zoom/2)+5*sin(2*PI*in/200)':
                y='ih/2-(ih/zoom/2)+3*cos(2*PI*in/250)':
                d=300:s=640x480,

        /* Add text overlay */
        drawtext=text='Test User':x=10:y=10:fontcolor=white:fontsize=16:
                box=1:boxcolor=black@0.5:boxborderw=5
    " \
    -c:v libx264 -preset ultrafast -crf 28 -pix_fmt yuv420p \
    -y webcam_test.mp4

    echo "Webcam simulation video created: webcam_test.mp4"
}

# Generate a simpler, faster test pattern as backup
generate_simple_test() {
    echo "Creating simple test pattern..."

    ffmpeg -f lavfi -i "testsrc2=duration=5:size=640x480:rate=30" \
        -vf "drawtext=text='ASCII-Chat Test':x=(w-text_w)/2:y=(h-text_h)/2:fontcolor=white:fontsize=30" \
        -c:v libx264 -preset ultrafast -crf 28 -pix_fmt yuv420p \
        -y test_pattern.mp4

    echo "Simple test pattern created: test_pattern.mp4"
}

# Main execution
echo "Starting video generation..."

# Try to generate the webcam simulation
if generate_webcam_simulation; then
    # Create symlink for default test
    ln -sf webcam_test.mp4 default_test.mp4 2>/dev/null || cp webcam_test.mp4 default_test.mp4
    echo "Success! Default test video is webcam_test.mp4"
else
    echo "Failed to generate webcam simulation, creating simple pattern..."
    generate_simple_test
    ln -sf test_pattern.mp4 default_test.mp4 2>/dev/null || cp test_pattern.mp4 default_test.mp4
fi

echo ""
echo "Test videos generated successfully!"
echo "Available videos:"
ls -la *.mp4 2>/dev/null | grep -E "\.mp4$" | awk '{print "  - " $NF}'