#!/bin/bash

handlerpre='<?xml version="1.0" encoding="utf-8"?>
<CIM CIMVERSION="2.0" DTDVERSION="2.0">
  <MESSAGE ID="4711" PROTOCOLVERSION="1.0">
    <SIMPLEREQ>
      <IMETHODCALL NAME="createInstance">
        <LOCALNAMESPACEPATH>
          <NAMESPACE NAME="root"/>
          <NAMESPACE NAME="interop"/>
        </LOCALNAMESPACEPATH>
        <IPARAMVALUE NAME="newinstance">
          <INSTANCE CLASSNAME="CIM_IndicationHandlerCIMXML">
            <PROPERTY NAME="systemcreationclassname" TYPE="string">
              <VALUE>CIM_ComputerSystem</VALUE>
            </PROPERTY>
            <PROPERTY NAME="SystemName" TYPE="string">
              <VALUE>localhost.localdomain</VALUE>
            </PROPERTY>
            <PROPERTY NAME="creationclassname" TYPE="string">
              <VALUE>CIM_IndicationHandlerCIMXML</VALUE>
            </PROPERTY>
            <PROPERTY NAME="Name" TYPE="string">
              <VALUE>Test_Indication_Handler_'

handlerpost='</VALUE>
            </PROPERTY>
            <PROPERTY NAME="destination" TYPE="string">
              <VALUE>file:///tmp/SFCBIndTest/SFCB_Listener.txt</VALUE>
            </PROPERTY>
          </INSTANCE>
        </IPARAMVALUE>
      </IMETHODCALL>
    </SIMPLEREQ>
  </MESSAGE>
</CIM>'

for i in {0..10} 
do
    echo "$handlerpre$i$handlerpost" > /tmp/wbemcat.xml
    wbemcat /tmp/wbemcat.xml
done

