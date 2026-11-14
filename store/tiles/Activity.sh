#!/bin/bash
cp -f /data/store/apps/activity/banana-activity /tmp/banana-activity
chmod +x /tmp/banana-activity
mkdir -p /data/banana/logs
exec /tmp/banana-activity >> /data/banana/logs/activity.log 2>&1
