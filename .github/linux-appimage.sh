# Taken from panda3ds linux-appimage.sh

# Prepare Tools for building the AppImage
wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
chmod a+x linuxdeploy-x86_64.AppImage

cp images/logo.png tanuki3ds.png
# Build AppImage
./linuxdeploy-x86_64.AppImage --appdir AppDir -d ./.github/Tanuki3DS.desktop  -e ./ctremu -i ./tanuki3ds.png --output appimage 