# RCSIDENT("$SiLK: lib-deps.mk 2b1e6827bdd5 2015-10-30 15:02:48Z mthomas $")

# Rules to build libraries that tools depend on

../libflowsource/libflowsource.la:
	@echo Making required library libflowsource
	cd ../libflowsource && $(MAKE) libflowsource.la

../libsilk/libsilk.la:
	@echo Making required library libsilk
	cd ../libsilk && $(MAKE) libsilk.la

../pysilk/libsilkpython.la:
	@echo Making required library libsilkpython
	cd ../pysilk && $(MAKE) libsilkpython.la

../../lua/src/liblua.la:
	@echo Making required library liblua
	cd ../../lua/src && $(MAKE) liblua.la
