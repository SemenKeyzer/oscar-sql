lessThan(QT_MAJOR_VERSION,6) {
    error("You need Qt 6 or newer to build OSCAR. 6.10 is recommended.");
}

TEMPLATE = subdirs

SUBDIRS += oscar

CONFIG += ordered
