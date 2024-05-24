#!/usr/bin/env bash

# Directory containing this script, to find the binary
script_dir=$(cd -- "$( dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)


if [[ -n "${MAC_DICTIONARY_FILE}" ]]; then
    body_data="${MAC_DICTIONARY_FILE}"
else

    case "$(uname -s)" in
	Darwin)
	    # Oxford Dictionary of English
	    body_data="/System/Library/AssetsV2/com_apple_MobileAsset_DictionaryServices_dictionaryOSX/14410040bdf8479c0ce189cc3ac9a0969c284bc7.asset/AssetData/Oxford Dictionary of English.dictionary/Contents/Resources/Body.data"

	    # # New Oxford American Dictionary
	    # body_data="/System/Library/AssetsV2/com_apple_MobileAsset_DictionaryServices_dictionaryOSX/b492c00313db19d14026cfac400e0918f233094c.asset/AssetData/New Oxford American Dictionary.dictionary/Contents/Resources/Body.data"
	    ;;

	Linux)
	    body_data="${script_dir}/14410040bdf8479c0ce189cc3ac9a0969c284bc7.asset/AssetData/Oxford Dictionary of English.dictionary/Contents/Resources/Body.data"
	    ;;
    esac
fi



# Check the dictionary file exists
if [ -z "${body_data}" ]; then
    echo "macDict.sh : no Body.data dictionary file set"
    exit 1
fi
if [ ! -f "${body_data}" ]; then
    echo "macDict.sh : dictionary file doesn't exist ${body_data}"
    exit 1
fi

# Walk up to the .asset dir
asset="${body_data}"
for i in {0..4}; do
    asset=$(dirname "${asset}")
done
asset=$(basename "${asset}")
if [[ ! "${asset}" =~ [a-zA-Z0-9]+\.asset$ ]]; then
    echo "macDict.sh : expecting Body.data path to include .asset directory"
    exit 1
fi

# Use the asset directory name as the filename for the cached index
key="${asset}"

# Where to read/write the index
cache_dir="${HOME}/.cache/macDict"
if [ ! -d "${cache_dir}" ]; then
    mkdir -p "${cache_dir}"
fi

# Dark mode
dark=""
if [[ "$(uname -s)" == "Linux" && "ubuntu:GNOME" == "${XDG_CURRENT_DESKTOP}" ]]; then
    if [[ "'prefer-dark'" == "$(gsettings get org.gnome.desktop.interface color-scheme)" ]]; then
	dark="-D"
    fi
fi

exec "${script_dir}/macDict" \
     -d "${body_data}" \
     -i "${cache_dir}/${key}" ${dark} -c \
     "$@"

