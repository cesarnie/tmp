rm *.o
rsync -av --exclude=".*/" * ../txspf2
cd ../txspf2
git commit -a -m %1
git push


