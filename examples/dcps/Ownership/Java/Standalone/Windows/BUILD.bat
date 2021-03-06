@ECHO OFF
mkdir  bld
mkdir  exec
cd bld
mkdir idlpp
mkdir classes
cd idlpp

REM
REM Generate java classes from IDL
REM
echo "Compile OwnershipData.idl"
idlpp -S -l java ../../../../../idl/OwnershipData.idl

REM
REM Compile generated java code
REM
echo "Compile generated Java classes"
echo === javac -classpath ".;%OSPL_HOME%/jar/dcpssaj.jar" -d ../classes ./OwnershipData/*.java
javac -classpath ".;%OSPL_HOME%/jar/dcpssaj.jar" -d ../classes ./OwnershipData/*.java
IF NOT ERRORLEVEL==0 (
  ECHO:
  ECHO *** Compilation of generated Java classes failed
  ECHO:
  GOTO error
)

REM
REM Compile application java code
REM
echo "Compile application Java classes"
cd ..
cd
javac -classpath .;idlpp;"%OSPL_HOME%/jar/dcpssaj.jar" -d ./classes ../../../src/*.java
IF NOT ERRORLEVEL==0 (
  ECHO:
  ECHO *** Compilation of application Java classes failed
  ECHO:
  GOTO error
)
REM
REM Create jar files
REM
echo "Create jar files"
cd ../exec
cd
jar cf OwnershipDataSubscriber.jar -C ../bld/classes . 
jar cf OwnershipDataPublisher.jar -C ../bld/classes .

cd ..
GOTO end

:error
ECHO An error occurred, exiting now
:end
