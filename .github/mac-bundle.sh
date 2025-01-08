# taken from Panda3DS mac-bundle.sh

# Taken from pcsx-redux create-app-bundle.sh
# For Plist buddy
PATH="$PATH:/usr/libexec"


# Construct the app iconset.
mkdir icon.iconset
convert images/logo.ico -alpha on -background none -units PixelsPerInch -density 72 -resize 16x16 icon.iconset/icon_16x16.png
convert images/logo.ico -alpha on -background none -units PixelsPerInch -density 144 -resize 32x32 icon.iconset/icon_16x16@2x.png
convert images/logo.ico -alpha on -background none -units PixelsPerInch -density 72 -resize 32x32 icon.iconset/icon_32x32.png
convert images/logo.ico -alpha on -background none -units PixelsPerInch -density 144 -resize 64x64 icon.iconset/icon_32x32@2x.png
convert images/logo.ico -alpha on -background none -units PixelsPerInch -density 72 -resize 128x128 icon.iconset/icon_128x128.png
convert images/logo.ico -alpha on -background none -units PixelsPerInch -density 144 -resize 256x256 icon.iconset/icon_128x128@2x.png
convert images/logo.ico -alpha on -background none -units PixelsPerInch -density 72 -resize 256x256 icon.iconset/icon_256x256.png
convert images/logo.ico -alpha on -background none -units PixelsPerInch -density 144 -resize 512x512 icon.iconset/icon_256x256@2x.png
convert images/logo.ico -alpha on -background none -units PixelsPerInch -density 72 -resize 512x512 icon.iconset/icon_512x512.png
convert images/logo.ico -alpha on -background none -units PixelsPerInch -density 144 -resize 1024x1024 icon.iconset/icon_512x512@2x.png
iconutil --convert icns icon.iconset
rm -r icon.iconset

# Set up the .app directory
mkdir -p Tanuki3DS.app/Contents/MacOS/
mkdir Tanuki3DS.app/Contents/Resources


# Copy binary into App
cp ./ctremu Tanuki3DS.app/Contents/MacOS/ctremu
chmod a+x Tanuki3DS.app/Contents/MacOS/ctremu

# Copy icons into App
mv icon.icns Tanuki3DS.app/Contents/Resources/AppIcon.icns

# Fix up Plist stuff
PlistBuddy Tanuki3DS.app/Contents/Info.plist -c "add CFBundleDisplayName string Tanuki3DS"
PlistBuddy Tanuki3DS.app/Contents/Info.plist -c "add CFBundleIconName string AppIcon"
PlistBuddy Tanuki3DS.app/Contents/Info.plist -c "add CFBundleIconFile string AppIcon"
PlistBuddy Tanuki3DS.app/Contents/Info.plist -c "add NSHighResolutionCapable bool true"
PlistBuddy Tanuki3DS.app/Contents/version.plist -c "add ProjectName string Tanuki3DS"

PlistBuddy Tanuki3DS.app/Contents/Info.plist -c "add CFBundleExecutable string ctremu"
PlistBuddy Tanuki3DS.app/Contents/Info.plist -c "add CFBundleDevelopmentRegion string en"
PlistBuddy Tanuki3DS.app/Contents/Info.plist -c "add CFBundleInfoDictionaryVersion string 6.0"
PlistBuddy Tanuki3DS.app/Contents/Info.plist -c "add CFBundleName string Tanuki3DS"
PlistBuddy Tanuki3DS.app/Contents/Info.plist -c "add CFBundlePackageType string APPL"

# Bundle dylibs
dylibbundler -od -b -x Tanuki3DS.app/Contents/MacOS/ctremu -d Tanuki3DS.app/Contents/Frameworks -p @rpath

# relative rpath
install_name_tool -add_rpath @loader_path/../Frameworks Tanuki3DS.app/Contents/MacOS/ctremu