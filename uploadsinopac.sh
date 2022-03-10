#!/usr/bin/expect
set fn [lindex $argv 0];

spawn sftp -P 2222 apex-upload@123.51.219.24
expect "apex-upload@123.51.219.24's password:"
send "dtR3xyEm\r"
expect "sftp>"
send "cd upload\r"
expect "ftp>"
send "put $fn\r"
expect "ftp>"
send "bye\r"
#interact
