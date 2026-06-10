向cardputer adv串口刷入固件命令

/Users/carlton/.espressif/python_env/idf5.4_py3.11_env/bin/python -m esptool --chip esp32s3 -p /dev/tty.usbmodem14201 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 8MB --flash_freq 80m 0x0 /Users/carlton/Documents/code/self/M5Cardputer-UserDemo/build/merged-binary.bin