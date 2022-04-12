#!/bin/bash

bin/regex_cli "^(\d+):(\S+):/.+" 6440:t0pfinnode:/paic/postgres/base/10.3:stg:master

bin/regex_cli "<title>(.*)</title>" "111 <title>Hello World</title> 222"