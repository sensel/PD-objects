//NOTE: This README is unfinished. I need to consult Dr. Ico first to make sure everything is okay with it.//

Welcome to the Sensel Pd object!

Overview of the files in this directory:
- libsensel.so: Sensel's precompiled library
- makefile: Exactly what it says
- m_pd.c and m_pd.h: Source and header for core Pd calls //do we need this in the official distribution?//
- sensel-c/: Official Sensel examples //may want to take out if it isn't okay to distribute this code freely//
- sensel.c: Source for Sensel Pd object
- sensel-help.pd: A Pd patch to explain the functionalities of the Sensel object
- sensel.pd_linux: //how should this file be explained? ask dr. ico//
- src/: Sensel source code //may want to take out if it isn't okay to distribute this code freely//

What you will need to do:
- Inside the Makefile, at the top there is a line that starts with PD_PATH=. After the equals sign, with no spaces, put the path to your Pure Data application.
- Copy the libsensel.so into your usr/local directory.
- //ask dr ico if im forgetting anything or if anything is wrong//
- Run the following commands:
	make clean
	make

After running make, you should be able to use the following command to run the help file:
sudo pd-l2ork sensel-help.pd