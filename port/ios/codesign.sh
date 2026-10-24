#!/bin/sh
# Sign the iOS app bundle (port/ios/ios.cmake, after each link).
#   codesign.sh APP IDENTITY [PROFILE]
# IDENTITY "-" signs ad-hoc (simulator). With a signing identity, PROFILE is
# the provisioning profile: it is embedded and its entitlements are used, with
# a wildcard app id ("TEAMID.*") narrowed to this app's bundle identifier.
set -e
app=$1
identity=$2
profile=$3

if [ -z "$profile" ]; then
    exec codesign --force --sign "$identity" --timestamp=none "$app"
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/ttyd-codesign.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
cp "$profile" "$app/embedded.mobileprovision"
security cms -D -i "$profile" > "$tmp/profile.plist"
/usr/libexec/PlistBuddy -x -c 'Print :Entitlements' "$tmp/profile.plist" > "$tmp/entitlements.plist"
bundle=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$app/Info.plist")
sed "s/\.\*<\/string>/.$bundle<\/string>/" "$tmp/entitlements.plist" > "$tmp/app.entitlements"
codesign --force --sign "$identity" --entitlements "$tmp/app.entitlements" --timestamp=none \
    --generate-entitlement-der "$app"
