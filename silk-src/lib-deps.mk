# RCSIDENT("$SiLK: lib-deps.mk 4dba2416c3d6 2015-09-10 19:03:20Z mthomas $")

# Rules to build libraries that tools depend on

../libflowsource/libflowsource.la:
	@echo Making required library libflowsource
	cd ../libflowsource && $(MAKE) libflowsource.la

../libsilk/libsilk-thrd.la:
	@echo Making required library libsilk-thrd
	cd ../libsilk && $(MAKE) libsilk-thrd.la

../libsilk/libsilk.la:
	@echo Making required library libsilk
	cd ../libsilk && $(MAKE) libsilk.la

../pysilk/libsilkpython.la:
	@echo Making required library libsilkpython
	cd ../pysilk && $(MAKE) libsilkpython.la
