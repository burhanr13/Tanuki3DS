
mkdir Tanuki3DS
cp ./ctremu.exe Tanuki3DS
for lib in $(ldd ./ctremu.exe | awk '{print $3}' | grep mingw); do
    echo $lib
    cp $lib Tanuki3DS
done