#################################################
############ PACKAGE & INSTALL ##################
#################################################

# Input variables:
#   - APP         : app name, e.g., probe
#   - INSTALL_DIR : directory to install MMT-Probe
#   - DPDK_CAPTURE: indicate wheter MMT-Probe uses DPDK
#   - REDIS_MODULE: indicate wheter MMT-Probe uses Redis
#   - KAFKA_MODULE: indicate wheter MMT-Probe uses Kafka
#   - MODULES_LIST: contains a list of modules that have been enable when compiling
#
#   - VERSION     :
#   - GIT_VERSION :
#   - QUIET       :
#   - RM          : remove file or folder
#   - MKDIR       : create a folder
 
#environment variables
SYS_NAME    = $(shell uname -s)
SYS_VERSION = $(shell uname -p)

#name of package files
PACKAGE_FILE_NAME = mmt-probe_$(VERSION)_$(GIT_VERSION)_$(SYS_NAME)_$(SYS_VERSION)
ifdef DPDK_CAPTURE
	PACKAGE_FILE_NAME := $(PACKAGE_FILE_NAME)_dpdk
else
	PACKAGE_FILE_NAME := $(PACKAGE_FILE_NAME)_pcap
endif


#List of packages mmt-probe depends on
RPM_DEPENDING_PACKAGES := mmt-dpi >= 1.6.13  #for .rpm file
DEB_DEPENDING_PACKAGES := mmt-dpi (>= 1.6.13)#for .def file
ifdef SECURITY_MODULE
	RPM_DEPENDING_PACKAGES += mmt-security >= 1.2.0
	DEB_DEPENDING_PACKAGES += mmt-security (>= 1.2.0)
endif

#temp folder to contain installed files
TEMP_DIR := /tmp/__mmt_probe_$(shell bash -c 'echo $$RANDOM')

#internal target to be used by others
# copy all necessary files to TEMP_DIR
--private-copy-files:
#create dir
	$(QUIET) $(RM) $(TEMP_DIR)
	$(QUIET) $(MKDIR) $(TEMP_DIR)/bin       \
		$(TEMP_DIR)/result/report/offline    \
		$(TEMP_DIR)/result/report/online     \
		$(TEMP_DIR)/result/behaviour/online  \
		$(TEMP_DIR)/result/behaviour/offline \
		$(TEMP_DIR)/result/security/online   \
		$(TEMP_DIR)/result/security/offline  \
		$(TEMP_DIR)/files                    \
      $(TEMP_DIR)/pcaps
#copy to bin
	$(QUIET) $(CP) $(APP)           $(TEMP_DIR)/bin/probe
#configuration file
	$(QUIET) $(CP) mmt_online.conf  $(TEMP_DIR)/mmt-probe.conf


#Check if having root permission to install MMT-Probe
--private-check-root:
	$(QUIET) [ "$$(id -u)" != "0" ] && echo "ERROR: Need root privilege" && exit 1 || true


#check if old version of MMT-Probe is existing
--private-check-old-version:
	$(QUIET) test -d $(INSTALL_DIR)                           \
		&& echo "ERROR: Old version of MMT-Probe is existing." \
		&& echo "($(INSTALL_DIR))"                             \
		&& exit 1                                              \
		|| true


#copy binary file to $PATH
USR_BIN_FILE_PATH     = /usr/bin/mmt-probe
#copy binary file to service list
ETC_SERVICE_FILE_PATH = /etc/init.d/mmt-probe


install: --private-check-old-version --private-check-root --private-copy-files
	$(QUIET) echo "Install MMT-Probe on $(INSTALL_DIR)"

	$(QUIET) $(MKDIR)  $(INSTALL_DIR)
	$(QUIET) $(CP) -r  $(TEMP_DIR)/* $(INSTALL_DIR)
	$(QUIET) $(RM) -rf $(TEMP_DIR) #remove temp_file
#create an alias
	$(QUIET) ln -s $(INSTALL_DIR)/bin/probe $(USR_BIN_FILE_PATH)
#create service
	$(QUIET) $(CP) daemon-service.sh  $(ETC_SERVICE_FILE_PATH)
	$(QUIET) chmod 0755               $(ETC_SERVICE_FILE_PATH)
	$(QUIET) systemctl daemon-reload
	@echo ""
	@echo "Successfully installed MMT-Probe on $(INSTALL_DIR)"
	@echo "You can start MMT-Probe by:"
	@echo " - either: sudo mmt-probe"
	@echo " - or    : sudo service mmt-probe start"


#internal target to be used to create distribution file: .deb or .rpm
--private-prepare-build: --private-copy-files
	$(QUIET) $(RM) $(PACKAGE_FILE_NAME) #remove old files

	$(QUIET) $(MKDIR) $(PACKAGE_FILE_NAME)/usr/bin/
	$(QUIET) ln -s $(INSTALL_DIR)/bin/probe $(PACKAGE_FILE_NAME)$(USR_BIN_FILE_PATH)

	$(QUIET) $(MKDIR) $(PACKAGE_FILE_NAME)/etc/ld.so.conf.d/
	@echo "$(INSTALL_DIR)/lib" >> $(PACKAGE_FILE_NAME)/etc/ld.so.conf.d/mmt-probe.conf

	$(QUIET) $(MKDIR) $(PACKAGE_FILE_NAME)/etc/init.d/
	$(QUIET) $(CP) daemon-service.sh  $(PACKAGE_FILE_NAME)$(ETC_SERVICE_FILE_PATH)
	$(QUIET) chmod 0755               $(PACKAGE_FILE_NAME)$(ETC_SERVICE_FILE_PATH)

	$(QUIET) $(MKDIR)  $(PACKAGE_FILE_NAME)$(INSTALL_DIR)
	$(QUIET) $(CP) -r  $(TEMP_DIR)/* $(PACKAGE_FILE_NAME)$(INSTALL_DIR)
	$(QUIET) $(RM) -rf $(TEMP_DIR) #remove temp_file
	
	$(QUIET) $(MKDIR) $(PACKAGE_FILE_NAME)$(INSTALL_DIR)/lib
ifdef REDIS_MODULE
	$(QUIET) $(CP) /usr/local/lib/libhiredis.so.*  $(PACKAGE_FILE_NAME)$(INSTALL_DIR)/lib
endif
ifdef KAFKA_MODULE
	$(QUIET) $(CP) /usr/local/lib/librdkafka.so.*  $(PACKAGE_FILE_NAME)$(INSTALL_DIR)/lib
endif
	$(QUIET) $(CP) /usr/lib/x86_64-linux-gnu/libconfuse.so.*  $(PACKAGE_FILE_NAME)$(INSTALL_DIR)/lib/


#build .deb file for Debian
deb: --private-prepare-build
	$(QUIET) $(MKDIR) $(PACKAGE_FILE_NAME)/DEBIAN
	$(QUIET) echo "Package: mmt-probe\
        \nVersion: $(VERSION)\
        \nSection: base\
        \nPriority: standard\
        \nDepends: $(DEB_DEPENDING_PACKAGES)\
        \nArchitecture: all \
        \nMaintainer: Montimage <contact@montimage.com> \
        \nDescription: MMT-Probe:\
        \n  Version id: $(GIT_VERSION).\
        \n  Build time: `date +"%Y-%m-%d %H:%M:%S"`\
        \n  Modules: $(MODULES_LIST)\
        \nHomepage: http://www.montimage.com"\
		> $(PACKAGE_FILE_NAME)/DEBIAN/control

	$(QUIET) dpkg-deb -b $(PACKAGE_FILE_NAME)
	$(QUIET) $(RM) $(PACKAGE_FILE_NAME)
	$(QUIET) $(RM) $(TEMP_DIR)


#build .rpm file for RedHat
rpm: --private-prepare-build
	$(QUIET) $(MKDIR) ./rpmbuild/{RPMS,BUILD}
	$(QUIET) echo -e\
      "Summary:  MMT-Probe\
      \nName: mmt-probe\
      \nVersion: $(VERSION)\
      \nRelease: $(GIT_VERSION)\
      \nLicense: proprietary\
      \nGroup: Development/Tools\
      \nURL: http://montimage.com/\
      \n\
      \nRequires:  $(RPM_DEPENDING_PACKAGES)\
      \nBuildRoot: %{_topdir}/BUILD/%{name}-%{version}-%{release}\
      \n\
      \n%description\
      \nMMT-Probe is a tool to analyze network traffic.\
      \nModules: $(MODULES_LIST) \
      \nBuild date: `date +"%Y-%m-%d %H:%M:%S"`\
      \n\
      \n%prep\
      \nrm -rf %{buildroot}\
      \nmkdir -p %{buildroot}\
      \ncp -r %{_topdir}/../$(PACKAGE_FILE_NAME)/* %{buildroot}/\
      \n\
      \n%clean\
      \nrm -rf %{buildroot}\
      \n\
      \n%files\
      \n%defattr(-,root,root,-)\
      \n/opt/mmt/probe/*\
      \n/usr/bin/mmt-probe\
      \n/etc/ld.so.conf.d/mmt-probe.conf\
      \n%post\
      \nldconfig\
   " > ./mmt-probe.spec

	$(QUIET) rpmbuild --quiet --rmspec --define "_topdir $(shell pwd)/rpmbuild"\
				 --define "_rpmfilename ../../$(PACKAGE_FILE_NAME).rpm" -bb ./mmt-probe.spec
	$(QUIET) $(RM) rpmbuild
	@echo "[PACKAGE] $(PACKAGE_FILE_NAME).rpm"

	$(QUIET) $(RM) $(PACKAGE_FILE_NAME)
	$(QUIET) $(RM) $(TEMP_DIR)


#stop mmt-probe service and remove it if exists
--private-stop-and-remove-service: --private-check-root
#check if file exists and not empty
	$(QUIET) [ -s $(ETC_SERVICE_FILE_PATH) ]                                  \
		&& update-rc.d -f mmt-probe remove                                     \
		&& $(RM) -rf $(ETC_SERVICE_FILE_PATH)                                  \
		&& systemctl daemon-reload                                             \
		&& echo "Removed MMT-Probe from service list $(ETC_SERVICE_FILE_PATH)" \
		|| true

#delete files generated by "install"
dist-clean: --private-stop-and-remove-service
	$(QUIET) $(RM) -rf $(USR_BIN_FILE_PATH)
	$(QUIET) $(RM) -rf $(INSTALL_DIR)
	@echo "Removed MMT-Probe from $(INSTALL_DIR)"
	@echo "Done"
	