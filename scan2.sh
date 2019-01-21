#!/bin/sh

# Scan-to button
# 1B 2A 00 0E 04 02 01 00 01 01 01 01 02 00
# 1B 2A 00 0E 04 02 01 00
# 01 - Menu choice number
# 01 - Resolution
# 01 - Color
# 01 - Format
# 02 - Size
# 00
# Non-zero values override the settings from the menu

# Debug
#logger -t scan2.sh -p user.debug MFP sent command: $1
#exec > /var/log/scan2log 2>&1
#set -x

PATH="$PATH:/opt/bin"

# http://wiki.bash-hackers.org/syntax/pattern
case $1 in
	2300000000000001) # Print-screen button full
		logger -t scan2.sh -p user.debug MFP sent print-screen full command
		;;
	2300000000000002) # Print-screen button active
		logger -t scan2.sh -p user.debug MFP sent print-screen active command
		;;
	1B2A000E04020100*) # Scan-to button
		logger -t scan2.sh -p user.debug MFP sent scan-to command: $1
		# Setting defaults as in scanimage --help -d 'xerox_mfp:libusb:001:002'
		resolution='150'
		mode='Color'
		format='pnm'
		geometry="-l 0 -t 0 -x 215.9 -y 297.18"
		# Menu choice (scan2menu.bin)
		case ${1:16:2} in
			01) resolution='300'
				mode='Color'
				format='tiff'
				geometry='-t 0 -l 2 -x 210 -y 297' # A4
				;;
			02) resolution='300'
				mode='Color'
				format='tiff'
				geometry='-t 0 -l 2 -x 210 -y 148' # A5h
				;;
			03) resolution='300'
				mode='Color'
				format='tiff'
				geometry="-l 0 -t 0 -x 215.9 -y 297.18" # Max
				;;
		esac
		case ${1:18:2} in
			01) resolution='75';;
			02) resolution='150';;
			04) resolution='200';;
			08) resolution='300';;
			10) resolution='600';;
		esac
		case ${1:20:2} in
			01) mode='Lineart';;
			02) mode='Gray';;
			04) mode='Halftone';; # 8bit (256) colors
			08) mode='Color';;
		esac
		case ${1:22:2} in
			01) format='pnm';; # BMP
			02) format='jpeg';;
			04) format='tiff';; # PDF
			05) format='tiff';;
		esac
		# https://en.wikipedia.org/wiki/Paper_size
		# SCX-4600 glass surface size 22x30cm
		# scanner skips of it 	1mm left 1mm right 2mm top 4mm bottom
		# + adds white bleeds	2mm left 2mm right 0mm top 2mm bottom
		# The real working area then is 214x292mm
		# "sane" max range for the model is 215.9x297.18mm
		if [ "${1:16:2}" != '02' ]; then
			case ${1:24:2} in
				01) geometry='-t 0 -l 2 -x 210 -y 297';; # A4
				02) geometry='-t 0 -l 2 -x 148 -y 210';; # A5
				04) geometry='-t 0 -l 2 -x 176 -y 250';; # B5
				05) geometry='-t 0 -l 2 -x 184 -y 267';; # Executive
				08) geometry='-t 0 -l 0 -x 215 -y 279';; # Letter 215.9 × 279.4
				21) geometry='-t 0 -l 2 -x 140 -y 216';; # Statement
				23) geometry='-t 0 -l 2 -x 182 -y 257';; # JIS B5
				25) geometry='-t 0 -l 2 -x 105 -y 148';; # A6
			esac
		else # Everything in landscape
			case ${1:24:2} in
				01) geometry='-t 0 -l 0 -x 215 -y 210';; # A4'
				02) geometry='-t 0 -l 2 -x 210 -y 148';; # A5
				04) geometry='-t 0 -l 0 -x 215 -y 176';; # B5'
				05) geometry='-t 0 -l 0 -x 215 -y 184';; # Executive'
				08) geometry='-t 0 -l 0 -x 215 -y 216';; # Letter'
				21) geometry='-t 0 -l 0 -x 215 -y 140';; # Statement
				23) geometry='-t 0 -l 0 -x 215 -y 182';; # JIS B5'
				25) geometry='-t 0 -l 2 -x 148 -y 105';; # A6
			esac
		fi
		# Autocount filename
		#fn='/tmp/scan'$(date +%Y%m%d-%H%M%S).$format
		dir=/mnt/share/SCANS
		cd "$dir"
		fn="$(ls -1 scan?????.* | tail -n 1)"
		fn="${fn:4:5}"
		# Remove leading zeros
		#while [[ $fn == 0* ]]; do fn="${fn#0}"; done
		fn="${fn#${fn%%[!0]*}}"
		fn="${dir}/scan$(printf '%05d' $((fn+1)))" #.$format
		# Execute
		#--device-name='samsung_mfp:libusb:001:002'
		arguments="--buffer-size=64 --resolution=$resolution --mode=$mode --format=pnm $geometry"
		logger -t scan2.sh -p user.debug "Going to scanimage -r $resolution -m $mode -f $format $geometry to $fn"
		#exit # debugging
		case $format in
			pnm) scanimage $arguments > "$fn.$format";;
			jpeg) case $mode in
					Lineart|Halftone) scanimage $arguments | ppm2tiff -R $resolution -c g4 "$fn.notjpg.tif";;
					Gray) scanimage $arguments | cjpeg -dct fast -grayscale -outfile "$fn.jpg";;
					*) scanimage $arguments | cjpeg -dct fast -outfile "$fn.jpg";; #-quality 75
				esac;;
			tiff) if [ "$mode" = 'Lineart' ] || [ "$mode" = 'Halftone' ];
					then scanimage $arguments | ppm2tiff -R $resolution -c g4 "$fn.tif" #g3 g4
					else scanimage $arguments | ppm2tiff -R $resolution -c zip "$fn.tif" #lzw zip
				fi;;
		esac
		;;
	*) logger -t scan2.sh -p user.notice MFP sent unknown command: $1;;
esac
