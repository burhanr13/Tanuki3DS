rm config.mk

for v in $@; do
    echo $v >> config.mk
done