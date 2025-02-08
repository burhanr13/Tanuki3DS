# Taken from panda3ds linux-appimage.sh

ARCH=$(uname -m)

# Prepare Tools for building the AppImage
wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage
chmod a+x linuxdeploy-${ARCH}.AppImage

cp images/logo.png tanuki3ds.png
# Build AppImage
./linuxdeploy-${ARCH}.AppImage --appdir AppDir -d ./.github/Tanuki3DS.desktop  -e ./ctremu -i ./tanuki3ds.png --output appimage 