mkdir -p bld
mkdir -p exec
cd bld
mkdir -p idlpp
mkdir -p classes
cd idlpp

#
# Generate java classes from IDL
#
echo "Compile DurabilityData.idl"
idlpp -S -l java ../../../../../idl/DurabilityData.idl

#
# Compile generated java code
#
echo "Compile generated Java classes"
$SPLICE_JAVAC -classpath .:$OSPL_HOME/jar/dcpssaj.jar:$SPLICE_EXTRA_CP -d ../classes ./DurabilityData/*.java


# Compile application java code
#
echo "Compile application Java classes"
cd ..
pwd
$SPLICE_JAVAC -classpath .:idlpp:$OSPL_HOME/jar/dcpssaj.jar:$SPLICE_EXTRA_CP -d ./classes ../../../src/*.java

#
# Create jar files
#
echo "Create jar files"
cd ../exec
pwd
jar cf DurabilityDataSubscriber.jar -C ../bld/classes . 
jar cf DurabilityDataPublisher.jar -C ../bld/classes .
cd ..
