# `fake-frog`

An Arduino-based project to build a frog-shaped temperature logger.
Contains code adapted from https://github.com/adafruit/Light-and-Temp-logger
and https://learn.adafruit.com/thermistor/using-a-thermistor.

## Created Files

Upon startup, the logger will create one or two files: a data file and
(optionally) a log file.  The default file name formats for these are
`dat_###.csv` and `log_###.txt`, where `###` is a number (meaning
there are can be up to 1000 log files and data files created).

### Log File

The creation of this log file is controlled by the `FILE_LOGGING` compile-time
setting. If set to `false`, no file will be created and no SD card logging will
occur. This is not recommended as the log file is useful for diagnosing
problems.

### Data File

The data file is in CSV format. In general, a data file will look like this:

```
Timestamp,Temp1,Temp2,Temp3,Temp4
0000-00-00T00:00:00,##.##,##.##,##.##,##.##
0000-00-00T00:00:00,##.##,##.##,##.##,##.##
...
```
