#! /bin/bash
sudo dpkg --get-selections |grep linux
sudo dpkg -i ./linux-headers-5.7.1.vwasp_5.7.1.vwasp-10.00.Custom_arm64.deb ./linux-image-5.7.1.vwasp_5.7.1.vwasp-10.00.Custom_arm64.deb
sudo dpkg --get-selections |grep linux
