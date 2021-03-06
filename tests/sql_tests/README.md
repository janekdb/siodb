# Siodb SQL tests

You must run those tests successfully in your environment before creating a pull request.

## Run the tests

You must specify at test runtime where compiled Siodb binaries are.
You can do that by either setting a value to the environment variable `SIODB_PATH`
or by indicating the path as the first argument of the script. For instance:

```bash
export SIODB_PATH=/opt/siodb/bin
./tests/sql_tests/run_test.sh
```

Or:

```bash
./tests/sql_tests/run_test.sh /opt/siodb/bin
```

## Troubleshooting

If the test fails (it does not return 0), then the instance is killed.
The log files, data files are kept at their location specified by the configuration file.

### Core dump

If the program fails, then a core dump should be generated by default by the OS.
If it is not the case, check that you have correctly configured the core dump behavior:

- On [Ubuntu](https://askubuntu.com/questions/966407/where-do-i-find-the-core-dump-in-ubuntu-16-04lts/1181036#1181036)
- On [CentOS](https://www.thegeekdiary.com/how-to-enable-core-dump-for-applications-on-centos-rhel)

## Contribute

Should we improve those tests? Please don't hesitate to:

- Open an [issue](https://github.com/siodb/siodb/issues)
- Fork this repository and submit a pull request
