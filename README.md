# multiotp-802.1x-peap-mschapv2
Patch for multiotp to support 802.1x eap/mschapv2

multiOTP supports MS-CHAP and MS-CHAPv2
multiOTP tokens will work with any type of PAP/CHAP/MS-CHAP/MS-CHAPv2 based authentication, including EAP-TTLS-PAP. 
BUT multiOTP DO NOT support peap/mschapv2.

FreeRADIUS support 802.1x eap/mschapv2,but no OTP

I will upload patch for multiOTP's FreeRADIUS soon.....

jradius test mschapv2
java -classpath jradius.jar:jradius-dictionary.jar:lib/java-getopt-1.0.10.jar:lib/gnu-crypto.jar:lib/log4j-1.2.13.jar 
    net.sf.jradius.client.RadClient -a MSCHAPv2 192.168.0.6  sjhh123 passfile
	
cat passfile
User-Name = ttu1
User-Password = 234557

//////////////////////////////////////////////////////////////////////////////////////////////////////////////

test PEAP/mschapv2
eapol_test -c otp -a 192.168.0.6 -p 1812 -s sjhh123
 cat /root/wpa_supplicant-2.6/cfg/otp

   eapol_test -c peap-mschapv2.conf -s testing123

network={
        ssid="example"
        key_mgmt=WPA-EAP
        #eap=PEAP
        eap=TTLS
        identity="ttu1"
        anonymous_identity="anonymous"
        password="234557"
        phase2="autheap=MSCHAPV2"

        #  Uncomment the following to perform server certificate validation.
	#  ca_cert="/etc/raddb/certs/ca.der"
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
