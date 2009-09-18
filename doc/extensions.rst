Extensions
**********

SQLite includes a number of extensions providing additional
functionality.  All extensions are disabled by default and you need to
take steps to enable them.

.. _ext-asyncvfs:

Asynchronous VFS
================

This extension does SQLite I/O in a background thread processing a
queue of requests.  To enable it you must have used :option:`fetch
--asyncvfs` to :file:`setup.py` at some point.  It is enabled by the
downloaded file :file:`sqlite3async.c` being present in the same
directory as :file:`setup.py`.  See the `SQLite documentation
<http://www.sqlite.org/asyncvfs.html>`__.

To use you must first call :func:`apsw.async_initialize` which will
register the VFS.  If you didn't make it the default VFS then you need
to specify it when opening your database.  From this point on, any
operations you perform that require writing to the database will be
queued and the database locked.  You should start a background thread
to perform the write operations calling :func:`apsw.async_run` to do
the work.  You can call :func:`apsw.async_control` to set and get
various things (eg adding delays, controlling locking) as well as
telling it when the writer code should exit.  This is a simple example::

    # Inherit from default vfs, do not make this the new default
    asyncvfsname=apsw.async_initialize("", False)
    # Open database
    db=apsw.Connection("database", vfs=asyncvfsname)
    # Make a worker thread
    t=threading.Thread(target=apsw.async_run)
    t.start()
    # do some work
    cur=db.cursor()
    cur.execute("..")
    # Close db
    db.close()
    # Tell worker to exit now
    apsw.async_control(apsw.SQLITEASYNC_HALT, apsw.SQLITEASYNC_HALT_NOW)

FTS3
====



Genfkey
=======

The code  
 is extracted from the SQLite shell (see `SQLite ticket 3687                          
 <http://www.sqlite.org/cvstrac/tktview?tn=3687>`__)                        


ICU
===

RTree
=====