/*--------------------------------------------------------------
 Machine Monitor by @radikalbytes 2017
--------------------------------------------------------------*/

// Libraries 
#include <dht.h>
#include <SPI.h>
#include <UIPEthernet.h>
#include <EEPROM.h> 
#include <Wire.h>
#include <DS1307RTC.h>
#include <Time.h>
#include <TimeAlarms.h>
#include <EEPROMex.h>

//EEPROM limites
const int memBase = 0;
const int maxAllowedWrites = 20;
//Estructura para hora y fecha
tmElements_t tm;
//Contenedor de fecha y hora
time_t T0;
// Variables de hora y fecha
int hora_;
int minuto_;
int segundo_;
int dia_;
int mes_;
int anno_;
// DHT Asignacion de Pines
dht DHT;
#define DHT11_PIN 6
#define DHT11_VCC 7
#define DHT11_GND 4

// Tamano del buffer asignado a captura de peticiones HTTP
#define REQ_BUF_SZ   50

// Variables Globales
float temperatura_= 0.0;
float humedad_ = 0.0;
float puntoderocio_ = 0.0;
char c;
char cc[20];
// MAC address
byte mac[6] = { }; 
//IPAddress ip(192,168,0,13); // IP address
EthernetServer server(80);  // create a server at port 80
EthernetClient client;
int numeroMaquina;
byte direccionIp[4]={};
byte gatewayIp[4]={};
byte maskIp[4]={};
int puertoServidor;
int numeroMuestras = 0;
int intervaloMuestras = 0;
int muestrasTemp[]={};
int muestrasHum[]={};
byte modoRespuesta;

char HTTP_req[REQ_BUF_SZ] = {0}; // Buffer de peticiones HTTP como null terminated string
char req_index = 0;              // Index en HTTP_req buffer

// Funcion Reset
void(* resetFunc) (void) = 0;//declare reset function at address 0

// Fahrenheit conversion function
double Fahrenheit(double celsius) {
  return ((double)(9/5) * celsius) + 32;
}

// Kelvin conversion function
double Kelvin(double celsius) {
 return celsius + 273.15;
}
 
// dewPoint function NOAA
//
double dewPoint(double celsius, double humidity) {
  // (1) Saturation Vapor Pressure = ESGG(T)
  double RATIO = 373.15 / (273.15 + celsius);
  double RHS = -7.90298 * (RATIO - 1);
  RHS += 5.02808 * log10(RATIO);
  RHS += -1.3816e-7 * (pow(10, (11.344 * (1 - 1/RATIO ))) - 1) ;
  RHS += 8.1328e-3 * (pow(10, (-3.49149 * (RATIO - 1))) - 1) ;
  RHS += log10(1013.246);
 
  // factor -3 is to adjust units - Vapor Pressure SVP * humidity
  double VP = pow(10, RHS - 3) * humidity;
 
  // (2) DEWPOINT = F(Vapor Pressure)
  double T = log(VP/0.61078);   // temp var
  return (241.88 * T) / (17.558 - T);
}
 
// delta max = 0.6544 wrt dewPoint()
// 6.9 x faster than dewPoint()
// reference: http://en.wikipedia.org/wiki/Dew_point
double dewPointFast(double celsius, double humidity) {
  double a = 17.271;
  double b = 237.7;
  double temp = (a * celsius) / (b + celsius) + log(humidity*0.01);
  double Td = (b * temp) / (a - temp);
  return Td;
}

void setup()
{
   int menu_time = millis();
   int menu_pause = millis();
   // enable Ethernet chip
   pinMode(53, OUTPUT);
   digitalWrite(53, HIGH);
   // Alimenta DHT
   pinMode(DHT11_VCC, OUTPUT);
   pinMode(DHT11_GND, OUTPUT);
   digitalWrite(DHT11_VCC, HIGH);
   digitalWrite(DHT11_GND, LOW);
   pinMode(DHT11_PIN, INPUT);         // Internal pullup activac
   digitalWrite(DHT11_PIN, HIGH);     // Pin to high (pull-up)
   // Config limites EEPROM
   EEPROMex.setMemPool(memBase, EEPROMSizeMega);
   EEPROMex.setMaxAllowedWrites(maxAllowedWrites);
   // Si no hay cargados valores en EEPROM
   // Ponerlos por defecto
   int xx_ = EEPROM.read(0);
   if (xx_ == 255) setEepromDefaults();
   cargaValoresServidor();
   // Serial init
   Serial.begin(115200);
   Serial.println("GKN MACHINE SERVER ");
   Serial.print("LIBRARY VERSION: ");
   Serial.println(DHT_LIB_VERSION);
   Serial.println();
   Serial.println("Pulsa m + intro para entrar al menu de configuracion");
   while (!Serial.available()){
     if ((millis()-menu_pause)>200){
       Serial.print(".");  // Un punto cada 200ms
       menu_pause=millis();
     }
     if ((millis()-menu_time)>5000) break;
   }
   if ((millis()-menu_time)<5000) { //5 segundos para entrar al menu
    printMenu();
    parseMenu();
   }
   Serial.println();
   Serial.print("Se va a iniciar el servidor ");
   sprintf(cc,"%X:%X:%X:%X:%X:%X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
   Serial.println(cc);
   Serial.print("En el puerto: ");
   Serial.println(puertoServidor);
   Serial.print("Con la direccion IP: ");
   sprintf(cc,"%u.%u.%u.%u",direccionIp[0],direccionIp[1],direccionIp[2],direccionIp[3]);
   Serial.println(cc);
   Serial.print("Gateway IP: ");
   sprintf(cc,"%u.%u.%u.%u",gatewayIp[0],gatewayIp[1],gatewayIp[2],gatewayIp[3]);
   Serial.println(cc);
   Serial.print("Subnet Mask: ");
   sprintf(cc,"%u.%u.%u.%u",maskIp[0],maskIp[1],maskIp[2],maskIp[3]);
   Serial.println(cc);
   ponFechaContenedor();
   iniciaCapturaDatos();
   IPAddress ip(direccionIp); // IP address
   IPAddress gw(gatewayIp);
   IPAddress msk(maskIp);
   Ethernet.begin(mac, ip, gw, gw, msk);  // inicializa Ethernet device
   server.begin(); // Inicia escucha clientes
}

void loop()
{
   Alarm.delay(0);
   
    client = server.available();  // try to get client
  
    if (client) {  // got client?
        boolean currentLineIsBlank = true;
        while (client.connected()) {
            if (client.available()) {   // client data available to read
                char c = client.read(); // read 1 byte (character) from client
                if (modoRespuesta==0){
                    // buffer first part of HTTP request in HTTP_req array (string)
                    // leave last element in array as 0 to null terminate string (REQ_BUF_SZ - 1)
                    if (req_index < (REQ_BUF_SZ - 1)) {
                        HTTP_req[req_index] = c;          // save HTTP request character
                        req_index++;
                    }
                    // last line of client request is blank and ends with \n
                    // respond to client only after last line received
                    if (c == '\n' && currentLineIsBlank) {
                        // send a standard http response header
                        client.println("HTTP/1.1 200 OK");
                        // remainder of header follows below, depending on if
                        // web page or XML page is requested
                        // Ajax request - send XML file
                        if (StrContains(HTTP_req, "ajax_inputs")) {
                            // send rest of HTTP header
                            client.println("Content-Type: text/xml");
                            client.println("Connection: keep-alive");
                            client.println();
                            // send XML file containing input states
                            XML_response(client);
                        }
                        else {  // web page request
                            // send rest of HTTP header
                            client.println("Content-Type: text/html");
                            client.println("Connection: keep-alive");
                            client.println();
                            // send web page
                            SendIndex(client);
                        }
                        // display received HTTP request on serial port
                        // reset buffer index and all buffer elements to 0
                        req_index = 0;
                        StrClear(HTTP_req, REQ_BUF_SZ);
                        break;
                    }
                    // every line of text received from the client ends with \r\n
                    if (c == '\n') {
                        // last character on line of received text
                        // starting new line with next character read
                        currentLineIsBlank = true;
                    }
                    else if (c != '\r') {
                        // a text character was received from client
                        currentLineIsBlank = false;
                    }
                } // End if

                else if (modoRespuesta == 1){
                  // if you've gotten to the end of the line (received a newline
                  // character) and the line is blank, the http request has ended,
                  // so you can send a reply
                  if (c == '\n' && currentLineIsBlank) 
                  {
                    client.println("HTTP/1.1 200 OK");
                    client.println("Content-Type: application/json");
                    client.println("Connection: close");  // the connection will be closed after completion of the response
                    client.println();
                    enviarDatosCSV();
                    break;
                  }

                  if (c == '\n') {
                    // you're starting a new line
                    currentLineIsBlank = true;
                  }
                  else if (c != '\r') {
                    // you've gotten a character on the current line
                    currentLineIsBlank = false;
                  }
                }// en elseif modoRespuesta 1
                
                else if (modoRespuesta == 2){
                  // if you've gotten to the end of the line (received a newline
                  // character) and the line is blank, the http request has ended,
                  // so you can send a reply
                  if (c == '\n' && currentLineIsBlank) 
                  {
                    client.println("HTTP/1.1 200 OK");
                    client.println("Content-Type: text/xml;charset=UTF-8");
                    client.println("Connection: close");  // the connection will be closed after completion of the response
                    client.println();
                    client.print("<WQ");
                    client.print(numeroMaquina);
                    client.println(">");
                    enviarDatosXML();
                    client.print("</WQ");
                    client.print(numeroMaquina);
                    client.println(">");
                    
                    break;
                  }

                  if (c == '\n') {
                    // you're starting a new line
                    currentLineIsBlank = true;
                  }
                  else if (c != '\r') {
                    // you've gotten a character on the current line
                    currentLineIsBlank = false;
                  }
                }// en elseif modoRespuesta 2
                

            } // end if (client.available())
        } // end while (client.connected())
        delay(1);      // give the web browser time to receive the data
        client.stop(); // close the connection
    } // end if (client)
}

/*
 *  Carga valores de la EEPROM
 */
void cargaValoresServidor(){

  EEPROM.get(0,direccionIp);
  EEPROM.get(4,puertoServidor);
  EEPROM.get(6,mac);
  EEPROM.get(12,numeroMuestras);
  EEPROM.get(14,intervaloMuestras);
  EEPROM.get(18, numeroMaquina);
  EEPROM.get(20, modoRespuesta);
  EEPROM.get(22, gatewayIp);
  EEPROM.get(26, maskIp);
  
}

/* 
 *  Send the XML file containing analog value
 */
void XML_response(EthernetClient cl)
{
    char sample;
    int chk = DHT.read22(DHT11_PIN);
    if ((chk) == DHTLIB_OK){
      humedad_ = DHT.humidity;
      temperatura_ = DHT.temperature;
      puntoderocio_ = dewPoint(DHT.temperature, DHT.humidity); 
    }    
    // Send values to AJAX code
    cl.print("<?xml version = \"1.0\" ?>");
    cl.print("<inputs>");
    
    cl.print("<analog>");
    cl.print(temperatura_);
    cl.print("</analog>");
    
    cl.print("<analog>");
    cl.print(humedad_);
    cl.print("</analog>");
    
    cl.print("<analog>");
    cl.print(puntoderocio_);
    cl.print("</analog>");
    
    cl.print("</inputs>");

    
}
//************* For my consideration *************************************
// Ojo al pasar el fichero html a string
// Las double quotes o " hay que escribirlas con un backslash delante: \"
// Y las backslash que aparezcan en el html tienen que ser dobles: \\
//************************************************************************

// HTML in program memory with javascript embebed in code

void SendIndex(EthernetClient client){
client.println(F( "<!DOCTYPE html>"));
client.println(F(  "<html>"));
client.println(F(  "<head>"));
client.println(F(  "<title>GKN Driveline Temperatura Armarios Electricos</title>"));
client.println(F(  "<script>"));
client.println(F(  "var data_val = 0;"));
client.println(F(  "var data_val2 = 0;"));
client.println(F(  "var data_val3 = 0;"));
client.println(F( "<!-- Gauge Code Starts -->"));
client.println(F( "var Gauge=function(b){function l(a,b){for(var c in b)\"object\"==typeof b[c]&&\"[object Array]\"!==Object.prototype.toString.call(b[c])&&\"renderTo\"!=c?(\"object\"!=typeof a[c]&&(a[c]={}),l(a[c],b[c])):a[c]=b[c]}function q(){z.width=b.width;z.height=b.height;A=z.cloneNode(!0);B=A.getContext(\"2d\");C=z.width;D=z.height;t=C/2;u=D/2;f=t<u?t:u;A.i8d=!1;B.translate(t,u);B.save();a.translate(t,u);a.save()}function v(a){var b=new Date;G=setInterval(function(){var c=(new Date-b)/a.duration;1<c&&(c=1);var f=(\"function\"=="));
client.println(F( "typeof a.delta?a.delta:M[a.delta])(c);a.step(f);1==c&&clearInterval(G)},a.delay||10)}function k(){G&&clearInterval(G);var a=I-n,h=n,c=b.animation;v({delay:c.delay,duration:c.duration,delta:c.fn,step:function(b){n=parseFloat(h)+a*b;E.draw()}})}function e(a){return a*Math.PI/180}function g(b,h,c){c=a.createLinearGradient(0,0,0,c);c.addColorStop(0,b);c.addColorStop(1,h);return c}function p(){var m=93*(f/100),h=f-m,c=91*(f/100),e=88*(f/100),d=85*(f/100);a.save();b.glow&&(a.shadowBlur=h,a.shadowColor="));
client.println(F( "\"rgba(0, 0, 0, 0.5)\");a.beginPath();a.arc(0,0,m,0,2*Math.PI,!0);a.fillStyle=g(\"#ddd\",\"#aaa\",m);a.fill();a.restore();a.beginPath();a.arc(0,0,c,0,2*Math.PI,!0);a.fillStyle=g(\"#fafafa\",\"#ccc\",c);a.fill();a.beginPath();a.arc(0,0,e,0,2*Math.PI,!0);a.fillStyle=g(\"#eee\",\"#f0f0f0\",e);a.fill();a.beginPath();a.arc(0,0,d,0,2*Math.PI,!0);a.fillStyle=b.colors.plate;a.fill();a.save()}function w(a){var h=!1;a=0===b.majorTicksFormat.dec?Math.round(a).toString():a.toFixed(b.majorTicksFormat.dec);return 1<b.majorTicksFormat[\"int\"]?"));
client.println(F( "(h=-1<a.indexOf(\".\"),-1<a.indexOf(\"-\")?\"-\"+(b.majorTicksFormat[\"int\"]+b.majorTicksFormat.dec+2+(h?1:0)-a.length)+a.replace(\"-\",\"\"):\"\"+(b.majorTicksFormat[\"int\"]+b.majorTicksFormat.dec+1+(h?1:0)-a.length)+a):a}function d(){var m=81*(f/100);a.lineWidth=2;a.strokeStyle=b.colors.majorTicks;a.save();if(0===b.majorTicks.length){for(var h=(b.maxValue-b.minValue)/5,c=0;5>c;c++)b.majorTicks.push(w(b.minValue+h*c));b.majorTicks.push(w(b.maxValue))}for(c=0;c<b.majorTicks.length;++c)a.rotate(e(45+c*(270/(b.majorTicks.length-"));
client.println(F( "1)))),a.beginPath(),a.moveTo(0,m),a.lineTo(0,m-15*(f/100)),a.stroke(),a.restore(),a.save();b.strokeTicks&&(a.rotate(e(90)),a.beginPath(),a.arc(0,0,m,e(45),e(315),!1),a.stroke(),a.restore(),a.save())}function J(){var m=81*(f/100);a.lineWidth=1;a.strokeStyle=b.colors.minorTicks;a.save();for(var h=b.minorTicks*(b.majorTicks.length-1),c=0;c<h;++c)a.rotate(e(45+c*(270/h))),a.beginPath(),a.moveTo(0,m),a.lineTo(0,m-7.5*(f/100)),a.stroke(),a.restore(),a.save()}function s(){for(var m=55*(f/100),h=0;h<b.majorTicks.length;++h){var c="));
client.println(F( "F(m,e(45+h*(270/(b.majorTicks.length-1))));a.font=20*(f/200)+\"px Arial\";a.fillStyle=b.colors.numbers;a.lineWidth=0;a.textAlign=\"center\";a.fillText(b.majorTicks[h],c.x,c.y+3)}}function x(a){var h=b.valueFormat.dec,c=b.valueFormat[\"int\"];a=parseFloat(a);var f=0>a;a=Math.abs(a);if(0<h){a=a.toFixed(h).toString().split(\".\");h=0;for(c-=a[0].length;h<c;++h)a[0]=\"0\"+a[0];a=(f?\"-\":\"\")+a[0]+\".\"+a[1]}else{a=Math.round(a).toString();h=0;for(c-=a.length;h<c;++h)a=\"0\"+a;a=(f?\"-\":\"\")+a}return a}function F(a,b){var c="));
client.println(F( "Math.sin(b),f=Math.cos(b);return{x:0*f-a*c,y:0*c+a*f}}function N(){a.save();for(var m=81*(f/100),h=m-15*(f/100),c=0,g=b.highlights.length;c<g;c++){var d=b.highlights[c],r=(b.maxValue-b.minValue)/270,k=e(45+(d.from-b.minValue)/r),r=e(45+(d.to-b.minValue)/r);a.beginPath();a.rotate(e(90));a.arc(0,0,m,k,r,!1);a.restore();a.save();var l=F(h,k),p=F(m,k);a.moveTo(l.x,l.y);a.lineTo(p.x,p.y);var p=F(m,r),n=F(h,r);a.lineTo(p.x,p.y);a.lineTo(n.x,n.y);a.lineTo(l.x,l.y);a.closePath();a.fillStyle=d.color;a.fill();"));
client.println(F( "a.beginPath();a.rotate(e(90));a.arc(0,0,h,k-0.2,r+0.2,!1);a.restore();a.closePath();a.fillStyle=b.colors.plate;a.fill();a.save()}}function K(){var m=12*(f/100),h=8*(f/100),c=77*(f/100),d=20*(f/100),k=4*(f/100),r=2*(f/100),l=function(){a.shadowOffsetX=2;a.shadowOffsetY=2;a.shadowBlur=10;a.shadowColor=\"rgba(188, 143, 143, 0.45)\"};l();a.save();n=0>n?Math.abs(b.minValue-n):0<b.minValue?n-b.minValue:Math.abs(b.minValue)+n;a.rotate(e(45+n/((b.maxValue-b.minValue)/270)));a.beginPath();a.moveTo(-r,-d);a.lineTo(-k,"));
client.println(F( "0);a.lineTo(-1,c);a.lineTo(1,c);a.lineTo(k,0);a.lineTo(r,-d);a.closePath();a.fillStyle=g(b.colors.needle.start,b.colors.needle.end,c-d);a.fill();a.beginPath();a.lineTo(-0.5,c);a.lineTo(-1,c);a.lineTo(-k,0);a.lineTo(-r,-d);a.lineTo(r/2-2,-d);a.closePath();a.fillStyle=\"rgba(255, 255, 255, 0.2)\";a.fill();a.restore();l();a.beginPath();a.arc(0,0,m,0,2*Math.PI,!0);a.fillStyle=g(\"#f0f0f0\",\"#ccc\",m);a.fill();a.restore();a.beginPath();a.arc(0,0,h,0,2*Math.PI,!0);a.fillStyle=g(\"#e8e8e8\",\"#f5f5f5\",h);a.fill()}"));
client.println(F( "function L(){a.save();a.font=40*(f/200)+\"px Led\";var b=x(y),h=a.measureText(\"-\"+x(0)).width,c=f-33*(f/100),g=0.12*f;a.save();var d=-h/2-0.025*f,e=c-g-0.04*f,h=h+0.05*f,g=g+0.07*f,k=0.025*f;a.beginPath();a.moveTo(d+k,e);a.lineTo(d+h-k,e);a.quadraticCurveTo(d+h,e,d+h,e+k);a.lineTo(d+h,e+g-k);a.quadraticCurveTo(d+h,e+g,d+h-k,e+g);a.lineTo(d+k,e+g);a.quadraticCurveTo(d,e+g,d,e+g-k);a.lineTo(d,e+k);a.quadraticCurveTo(d,e,d+k,e);a.closePath();d=a.createRadialGradient(0,c-0.12*f-0.025*f+(0.12*f+0.045*f)/"));
client.println(F( "2,f/10,0,c-0.12*f-0.025*f+(0.12*f+0.045*f)/2,f/5);d.addColorStop(0,\"#888\");d.addColorStop(1,\"#666\");a.strokeStyle=d;a.lineWidth=0.05*f;a.stroke();a.shadowBlur=0.012*f;a.shadowColor=\"rgba(0, 0, 0, 1)\";a.fillStyle=\"#babab2\";a.fill();a.restore();a.shadowOffsetX=0.004*f;a.shadowOffsetY=0.004*f;a.shadowBlur=0.012*f;a.shadowColor=\"rgba(0, 0, 0, 0.3)\";a.fillStyle=\"#444\";a.textAlign=\"center\";a.fillText(b,-0,c);a.restore()}Gauge.Collection.push(this);this.config={renderTo:null,width:200,height:200,title:!1,"));
client.println(F( "maxValue:100,minValue:0,majorTicks:[],minorTicks:10,strokeTicks:!0,units:!1,valueFormat:{\"int\":3,dec:2},majorTicksFormat:{\"int\":1,dec:0},glow:!0,animation:{delay:10,duration:250,fn:\"cycle\"},colors:{plate:\"#fff\",majorTicks:\"#444\",minorTicks:\"#666\",title:\"#888\",units:\"#888\",numbers:\"#444\",needle:{start:\"rgba(240, 128, 128, 1)\",end:\"rgba(255, 160, 122, .9)\"}},highlights:[{from:20,to:60,color:\"#eee\"},{from:60,to:80,color:\"#ccc\"},{from:80,to:100,color:\"#999\"}]};var y=0,E=this,n=0,I=0,H=!1;this.setValue="));
client.println(F( "function(a){n=b.animation?y:a;var d=(b.maxValue-b.minValue)/100;I=a>b.maxValue?b.maxValue+d:a<b.minValue?b.minValue-d:a;y=a;b.animation?k():this.draw();return this};this.setRawValue=function(a){n=y=a;this.draw();return this};this.clear=function(){y=n=I=this.config.minValue;this.draw();return this};this.getValue=function(){return y};this.onready=function(){};l(this.config,b);this.config.minValue=parseFloat(this.config.minValue);this.config.maxValue=parseFloat(this.config.maxValue);b=this.config;n="));
client.println(F( "y=b.minValue;if(!b.renderTo)throw Error(\"Canvas element was not specified when creating the Gauge object!\");var z=b.renderTo.tagName?b.renderTo:document.getElementById(b.renderTo),a=z.getContext(\"2d\"),A,C,D,t,u,f,B;q();this.updateConfig=function(a){l(this.config,a);q();this.draw();return this};var M={linear:function(a){return a},quad:function(a){return Math.pow(a,2)},quint:function(a){return Math.pow(a,5)},cycle:function(a){return 1-Math.sin(Math.acos(a))},bounce:function(a){a:{a=1-a;for(var b=0,"));
client.println(F( "c=1;;b+=c,c/=2)if(a>=(7-4*b)/11){a=-Math.pow((11-6*b-11*a)/4,2)+Math.pow(c,2);break a}a=void 0}return 1-a},elastic:function(a){a=1-a;return 1-Math.pow(2,10*(a-1))*Math.cos(30*Math.PI/3*a)}},G=null;a.lineCap=\"round\";this.draw=function(){if(!A.i8d){B.clearRect(-t,-u,C,D);B.save();var g={ctx:a};a=B;p();N();J();d();s();b.title&&(a.save(),a.font=24*(f/200)+\"px Arial\",a.fillStyle=b.colors.title,a.textAlign=\"center\",a.fillText(b.title,0,-f/4.25),a.restore());b.units&&(a.save(),a.font=22*(f/200)+\"px Arial\","));
client.println(F( "a.fillStyle=b.colors.units,a.textAlign=\"center\",a.fillText(b.units,0,f/3.25),a.restore());A.i8d=!0;a=g.ctx;delete g.ctx}a.clearRect(-t,-u,C,D);a.save();a.drawImage(A,-t,-u,C,D);if(Gauge.initialized)L(),K(),H||(E.onready&&E.onready(),H=!0);else var e=setInterval(function(){Gauge.initialized&&(clearInterval(e),L(),K(),H||(E.onready&&E.onready(),H=!0))},10);return this}};Gauge.initialized=!1;"));
client.println(F( "(function(){var b=document,l=b.getElementsByTagName(\"head\")[0],q=-1!=navigator.userAgent.toLocaleLowerCase().indexOf(\"msie\"),v=\"@font-face {font-family: 'Led';src: url('fonts/digital-7-mono.\"+(q?\"eot\":\"ttf\")+\"');}\",k=b.createElement(\"style\");k.type=\"text/css\";if(q)l.appendChild(k),l=k.styleSheet,l.cssText=v;else{try{k.appendChild(b.createTextNode(v))}catch(e){k.cssText=v}l.appendChild(k);l=k.styleSheet?k.styleSheet:k.sheet||b.styleSheets[b.styleSheets.length-1]}var g=setInterval(function(){if(b.body){clearInterval(g);"));
client.println(F( "var e=b.createElement(\"div\");e.style.fontFamily=\"Led\";e.style.position=\"absolute\";e.style.height=e.style.width=0;e.style.overflow=\"hidden\";e.innerHTML=\".\";b.body.appendChild(e);setTimeout(function(){Gauge.initialized=!0;e.parentNode.removeChild(e)},250)}},1)})();Gauge.Collection=[];"));
client.println(F( "Gauge.Collection.get=function(b){if(\"string\"==typeof b)for(var l=0,q=this.length;l<q;l++){if((this[l].config.renderTo.tagName?this[l].config.renderTo:document.getElementById(this[l].config.renderTo)).getAttribute(\"id\")==b)return this[l]}else return\"number\"==typeof b?this[b]:null};function domReady(b){window.addEventListener?window.addEventListener(\"DOMContentLoaded\",b,!1):window.attachEvent(\"onload\",b)}"));
client.println(F( "domReady(function(){function b(b){for(var e=b[0],d=1,g=b.length;d<g;d++)e+=b[d].substr(0,1).toUpperCase()+b[d].substr(1,b[d].length-1);return e}for(var l=document.getElementsByTagName(\"canvas\"),q=0,v=l.length;q<v;q++)if(\"canv-gauge\"==l[q].getAttribute(\"data-type\")){var k=l[q],e={},g,p=parseInt(k.getAttribute(\"width\"),10),w=parseInt(k.getAttribute(\"height\"),10);e.renderTo=k;p&&(e.width=p);w&&(e.height=w);p=0;for(w=k.attributes.length;p<w;p++)if(g=k.attributes.item(p).nodeName,\"data-type\"!=g&&\"data-\"=="));
client.println(F( "g.substr(0,5)){var d=g.substr(5,g.length-5).toLowerCase().split(\"-\");if(g=k.getAttribute(g))switch(d[0]){case \"colors\":d[1]&&(e.colors||(e.colors={}),\"needle\"==d[1]?(d=g.split(/\\s+/),e.colors.needle=d[0]&&d[1]?{start:d[0],end:d[1]}:g):(d.shift(),e.colors[b(d)]=g));break;case \"highlights\":e.highlights||(e.highlights=[]);g=g.match(/(?:(?:-?\\d*\\.)?(-?\\d+){1,2} ){2}(?:(?:#|0x)?(?:[0-9A-F|a-f]){3,8}|rgba?\\(.*?\\))/g);for(var d=0,J=g.length;d<J;d++){var s=g[d].replace(/^\\s+|\\s+$/g,\"\").split(/\\s+/),x={};"));
client.println(F( "s[0]&&\"\"!=s[0]&&(x.from=s[0]);s[1]&&\"\"!=s[1]&&(x.to=s[1]);s[2]&&\"\"!=s[2]&&(x.color=s[2]);e.highlights.push(x)}break;case \"animation\":d[1]&&(e.animation||(e.animation={}),\"fn\"==d[1]&&/^\\s*function\\s*\\(/.test(g)&&(g=eval(\"(\"+g+\")\")),e.animation[d[1]]=g);break;default:d=b(d);if(\"onready\"==d)continue;if(\"majorTicks\"==d)g=g.split(/\\s+/);else if(\"strokeTicks\"==d||\"glow\"==d)g=\"true\"==g?!0:!1;else if(\"valueFormat\"==d)if(g=g.split(\".\"),2==g.length)g={\"int\":parseInt(g[0],10),dec:parseInt(g[1],10)};else continue;"));
client.println(F( "e[d]=g}}e=new Gauge(e);k.getAttribute(\"data-value\")&&e.setRawValue(parseFloat(k.getAttribute(\"data-value\")));k.getAttribute(\"data-onready\")&&(e.onready=function(){eval(this.config.renderTo.getAttribute(\"data-onready\"))});e.draw()}});window.Gauge=Gauge;"));
client.println(F( "<!-- Gauge Code Ends -->"));
client.println(F(  "function GetArduinoInputs()"));
client.println(F(  "{"));
client.println(F(  "nocache = \"&nocache=\" + Math.random() * 1000000;"));
client.println(F(  "var request = new XMLHttpRequest();"));
client.println(F(  "request.onreadystatechange = function()"));
client.println(F(  "{"));
client.println(F(  "             if (this.readyState == 4) {"));
client.println(F(  "                    if (this.status == 200) {"));
client.println(F(  "                      if (this.responseXML != null) {"));
client.println(F(  "                          document.getElementById(\"input3\").innerHTML ="));
client.println(F(  "                              this.responseXML.getElementsByTagName('analog')[0].childNodes[0].nodeValue;"));
client.println(F(  "                          document.getElementById(\"input4\").innerHTML ="));
client.println(F(  "                              this.responseXML.getElementsByTagName('analog')[1].childNodes[0].nodeValue;"));
client.println(F(  "                          document.getElementById(\"input5\").innerHTML ="));
client.println(F(  "                              this.responseXML.getElementsByTagName('analog')[2].childNodes[0].nodeValue;"));
client.println(F(  "                          data_val  = this.responseXML.getElementsByTagName('analog')[0].childNodes[0].nodeValue;"));
client.println(F(  "                          data_val2 = this.responseXML.getElementsByTagName('analog')[1].childNodes[0].nodeValue;"));
client.println(F(  "                          data_val3 = this.responseXML.getElementsByTagName('analog')[2].childNodes[0].nodeValue;"));

client.println(F(  "                        }"));
client.println(F(  "                    }"));
client.println(F(  "                }"));
client.println(F(  "            }"));
client.println(F(  "            request.open(\"GET\", \"ajax_inputs\" + nocache, true);"));
client.println(F(  "            request.send(null);"));
client.println(F(  "            setTimeout('GetArduinoInputs()', 200);"));
client.println(F(  "      }"));
client.println(F(  "    </script>"));
client.println(F(  "    </head>"));
client.println(F(  "    <body onload=\"GetArduinoInputs()\">"));
client.print(F(  "        <h1>Monitor Machine WQ"));
client.print(numeroMaquina);
client.println(F(  "</h1>"));
client.println(F(  "        <p>Temperatura: <span id=\"input3\">...</span></p>"));
client.println(F(  "        <p>Humedad: <span id=\"input4\">...</span></p>"));
client.println(F(  "        <p>Punto de Rocio: <span id=\"input5\">...</span></p>"));
client.println(F( "        <canvas id=\"an_gauge_1\" data-title=\"Temperature\" data-units=\"Temp. &deg;C\" data-major-ticks=\"-40 -30 -20 -10 0 10 20 30 40 50 60 70 80 90 100 110 120 130 140 150\" data-type=\"canv-gauge\" data-min-value=\"-40\" data-max-value=\"150\" data-highlights=\"-40 0 #4D89F2, 0 10 #25B8D9, 10 30 #0BB950, 30 50 #cc5, 50 150 #f33\"  data-onready=\"setInterval( function() { Gauge.Collection.get('an_gauge_1').setValue(data_val);}, 200);\"></canvas>"));
client.println(F( "        <canvas id=\"an_gauge_2\" data-title=\"Humidity\" data-units=\"Hum. &percnt;\" data-major-ticks=\"0 10 20 30 40 50 60 70 80 90 100\" data-type=\"canv-gauge\" data-min-value=\"0\" data-max-value=\"100\" data-highlights=\"0 10 #f33, 10 30 #cc5, 30 40 #0BB950,40 80 #25B8D9,80 100 #4D89F2 \" data-onready=\"setInterval( function() { Gauge.Collection.get('an_gauge_2').setValue(data_val2);}, 200);\"></canvas>"));
client.println(F( "        <canvas id=\"an_gauge_3\" data-title=\"Dew Point\" data-units=\"Temp. &deg;C\" data-major-ticks=\"0 5 10 15 20 25 30 35 40 45 50\" data-type=\"canv-gauge\" data-min-value=\"0\" data-max-value=\"50\" data-highlights=\"0 100 #4D89F2 \" data-onready=\"setInterval( function() { Gauge.Collection.get('an_gauge_3').setValue(data_val3);}, 200);\"></canvas>"));
client.println(F("<H3>Design by @radikalbytes</H3>"));
client.println(F(  "    </body>"));
client.println(F(  "</html>"));
}



// sets every element of str to 0 (clears array)
void StrClear(char *str, char length)
{
    for (int i = 0; i < length; i++) {
        str[i] = 0;
    }
}

// searches for the string sfind in the string str
// returns 1 if string found
// returns 0 if string not found
char StrContains(char *str, char *sfind)
{
    char found = 0;
    char index = 0;
    char len;

    len = strlen(str);

    if (strlen(sfind) > len) {
        return 0;
    }
    while (index < len) {
        if (str[index] == sfind[found]) {
            found++;
            if (strlen(sfind) == found) {
                return 1;
            }
        }
        else {
            found = 0;
        }
        index++;
    }

    return 0;
}

/*
 * Saca menu de configuracion por puerto serie
 */
void printMenu()
{
  ponFechaSerie();
  Serial.println();
  Serial.println(F("/////////////////////////////////////////////"));
  Serial.println(F("// Menu de configuracion Sensores Maquinas //"));
  Serial.println(F("/////////////////////////////////////////////"));
  Serial.println();
  Serial.println(F("0) Configurar numero de maquina"));
  Serial.println(F("1) Configurar IP"));
  Serial.println(F("2) Configurar puerto HTTP"));
  Serial.println(F("3) Configurar Mac Address"));
  Serial.println(F("4) Numero de muestras"));
  Serial.println(F("5) Intervalo entre muestras"));
  Serial.println(F("6) Mostar datos de configuracion"));
  Serial.println(F("7) Configurar fecha y hora RTC interno"));
  Serial.println(F("8) Muestra datos EEPROM"));
  Serial.println(F("9) Reset"));
  Serial.println(F("G) Configurar Gateway IP"));
  Serial.println(F("S) Configurar Subnet Mask"));
  Serial.println(F("C) Configurar modo de Respuesta"));
  Serial.println(F("R) Restaurar valores por defecto"));
  Serial.println(); 
}

/*
 * Parsea los comandos de entrada del menu
 */
void parseMenu(){
  char c;
  int ii,iii;
  while (1){
    c = Serial.read();
    switch (c) {
      case '0':
        configuraNumMaquina();
      break;
      case '1':
        configuraIp();
      break;
      
      case '2':
        //configuraPuerto();
      break;

      case '3':
        configuraMac();
        
      break;

      case '4':
        configuraMuestras();
      break;

      case '5':
        configuraIntervalo();        
      break;

      case '6':
        muestraDatosConfig();
        Serial.println(); 
        printMenu();
      break;

      case '7':
        configuraHora();
      break;

      case '8':
        muestraDatosEeprom();
      break;

      case '9':
        resetFunc();  //call reset
      break;
      
      case 'g'|'G':
        configuraGateway();
      break;

      case 's'|'S':
        configuraMask();
      break;
      
      case 'r'|'R':
        for (int y=0;y<30;y++) EEPROM.write(y,255);
        resetFunc();  //call reset
      break;

      case 'c'|'C':
        configuraModoRespuesta();
      break;
    } // end case
  }  // end while
}

/****************** Estructura de EEPROM ******************
 ** 0-3 --------> Direccion IP 
 *  4-5 ---------> Puerto Http servidor
 *  6-11 -------> Mac Address
 *  12-13 ---------> Numero de muestras a capturar
 *  14-15 ------> Segundos entre muestras (max 65535)
 *  16-17 ------> Puntero de muestras
 *  18-19 ------> Numero de maquina
 *  20 ---------> Modo respuesta
 *  22-25 ------> Gateway Ip
 *  26-29 ------> Subnet Mask
 *  100-3684 ---> Array de Muestras 0x64 - 0x0E64
 *          |---> Dia (byte)
 *          |---> Mes (byte)
 *          |---> Anno (byte)
 *          |---> Hora (byte)
 *          |---> Minuto (byte)
 *          |---> Segundo (byte)
 *          |---> Temperatura (float: 4bytes)
 *          |---> Humedad (float:4bytes)
 *     Total = 14 bytes por muestra
 **********************************************************/
void setEepromDefaults(){
  byte dirIp[4] = {192,168,0,55};
  byte gateway_[4] = {192,168,0,1};
  byte subnetMask_[4] = {255,255,255,0}; 
  int puerto = 80;
  byte macAddress_[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};
  int numeroMuestras = 50;
  int muestrasTiempo = 600;
  int punteroMuestras = 0;
  // Escribe datos
  for (int i=0;i<4;i++)  EEPROM.update (i,dirIp[i]);
  EEPROMWriteInt (4,puerto);
  for (int i=6;i<12;i++)  EEPROM.update (i,macAddress_[i-6]);
  EEPROMWriteInt (12,numeroMuestras);
  EEPROMWriteInt (14,muestrasTiempo);
  EEPROMWriteInt (16,punteroMuestras);
  EEPROMWriteInt (18,0); // Numero de maquina
  EEPROM.update(20, 0); // Modo HTML por defecto
  for (int i=0;i<4;i++)  EEPROM.update (22+i,gateway_[i]);
  for (int i=0;i<4;i++)  EEPROM.update (26+i,subnetMask_[i]);
  //Borrar datos EEPROM de 256 muestras
  for (int iii = 100; iii<3685; iii++){
    EEPROM.update (iii,0);
  }
  capturaMuestra();
}

// Mostrar datos de configuracion por el puerto serie
void muestraDatosConfig(){
  byte dirIp_[4];
  byte gateway_[4];
  byte subnetMask_[4];
  int puerto_;
  byte macAddress_[6];
  int numeroMuestras_;
  int muestrasTiempo_;
  char s[5];
  
  EEPROM.get(0,dirIp_);
  EEPROM.get(4,puerto_);
  EEPROM.get(6,macAddress_);
  EEPROM.get(12,numeroMuestras_);
  EEPROM.get(14,muestrasTiempo_);
  EEPROM.get(22, gateway_);
  EEPROM.get(26, subnetMask_);
  Serial.println();
  Serial.println(F("*******Datos de dispositivo*******"));
  Serial.print ("Direccion IP: ");
  for (int cc=0;cc<4;cc++){
    Serial.print (dirIp_[cc]);
    if (cc<3) Serial.print(",");
  }
  Serial.print ("\nGateway IP: ");
  for (int cc=0;cc<4;cc++){
    Serial.print (gateway_[cc]);
    if (cc<3) Serial.print(",");
  }
  Serial.print ("\nSubnet Mask: ");
  for (int cc=0;cc<4;cc++){
    Serial.print (subnetMask_[cc]);
    if (cc<3) Serial.print(",");
  }

  Serial.print("\nPuerto Http: ");
  Serial.println(puerto_);
  Serial.print("Mac Address: ");
  for (int cc=0;cc<6;cc++){
    sprintf(s,"0x%02X",macAddress_[cc]);
    Serial.print (s);
    if (cc<5) Serial.print(",");
  }
  Serial.println();
  Serial.print("Numero de muestras a guardar: ");
  Serial.println(numeroMuestras_);
  Serial.print("Intervalo entre muestras (en segundos): ");
  Serial.println(muestrasTiempo_);
  Serial.println();

}
/*
 *  Configura Hora y fecha RTC interno
 */
void configuraHora(){
  String a;
  int hora_[3];
  int fecha_[3];
  bool fallo = false;
  Serial.println("Introduce fecha (formato DD/MM/YYYY)");
  while(Serial.available()==0);
  a = Serial.readString();
  // Asignar formato y check valid 
  if (sscanf(a.c_str(), "%d/%d/%d", &fecha_[0], &fecha_[1], &fecha_[2]) != 3){
    Serial.println("La fecha es incorrecta");
    fallo = true;
  }
  else{
    tm.Day = fecha_[0];
    tm.Month = fecha_[1];
    tm.Year = CalendarYrToTm(fecha_[2]); 
  }
  
  Serial.println("Introduce hora (formato 24h HH:MM:ss)");
  while(Serial.available()==0);
  a = Serial.readString();
  // Asignar formato y check valid
  if (sscanf(a.c_str(), "%d:%d:%d", &hora_[0], &hora_[1], &hora_[2]) != 3){
    Serial.println("La hora es incorrecta");
    fallo = true;
  }
  else{
    tm.Hour = hora_[0];
    tm.Minute = hora_[1];
    tm.Second = hora_[2];
  } 
  if(!fallo) RTC.write(tm);
  printMenu();
  
 }


 
/*
 * Introducir nueva direccion IP desde la configuracion
 */
void configuraIp(){
  String a;
  byte ipAddr[4];
  Serial.println("Introduce direccion IP (formato XX.XX.XX.XX)");
  while(Serial.available()==0);
  a = Serial.readString();
  // Asignar formato
  sscanf(a.c_str(), "%u.%u.%u.%u", &ipAddr[0], &ipAddr[1], &ipAddr[2], &ipAddr[3]);
  if ((ipAddr[0]>255) || (ipAddr[1]>255) || (ipAddr[2]>255) || (ipAddr[3]>255) ){
    Serial.println("La direccion IP es incorrecta");
  }
  else{
    Serial.print ("La nueva direccion IP es: ");
    Serial.println(a);
    for (int i=0;i<4;i++)  EEPROM.update (i,ipAddr[i]);
    Serial.println(); 
    printMenu();
  }  
}

/*
 * Introducir nueva direccion IP del Gateway
 */
void configuraGateway(){
  String a;
  byte gateway_[4];
  Serial.println("Introduce direccion IP del Gateway (formato XX.XX.XX.XX)");
  while(Serial.available()==0);
  a = Serial.readString();
  // Asignar formato
  sscanf(a.c_str(), "%u.%u.%u.%u", &gateway_[0], &gateway_[1], &gateway_[2], &gateway_[3]);
  if ((gateway_[0]>255) || (gateway_[1]>255) || (gateway_[2]>255) || (gateway_[3]>255) ){
    Serial.println("La direccion IP es incorrecta");
  }
  else{
    Serial.print ("La nueva direccion IP del Gateway es: ");
    Serial.println(a);
    for (int i=0;i<4;i++)  EEPROM.update (22+i,gateway_[i]);
    Serial.println(); 
    printMenu();
  }  
}

/*
 * Introducir nueva direccion IP del Gateway
 */
void configuraMask(){
  String a;
  byte mask_[4];
  Serial.println("Introduce Subnet mask (formato XX.XX.XX.XX)");
  while(Serial.available()==0);
  a = Serial.readString();
  // Asignar formato
  sscanf(a.c_str(), "%u.%u.%u.%u", &mask_[0], &mask_[1], &mask_[2], &mask_[3]);
  if ((mask_[0]>255) || (mask_[1]>255) || (mask_[2]>255) || (mask_[3]>255) ){
    Serial.println("La subnet mask es incorrecta");
  }
  else{
    Serial.print ("La nueva Subnet mask es: ");
    Serial.println(a);
    for (int i=0;i<4;i++)  EEPROM.update (26+i,mask_[i]);
    Serial.println(); 
    printMenu();
  }  
}
/*
 * Introducir nueva direccion Mac desde la configuracion
 */
void configuraMac(){
  String a;
  int macAddr[6];
  Serial.println("Introduce direccion MAC (formato AA:BB:CC:DD:EE:FF)");
  while(Serial.available()==0);
  a = Serial.readString();
  // Asignar formato y check valid
  if (sscanf(a.c_str(), "%x:%x:%x:%x:%x:%x", &macAddr[0], &macAddr[1], &macAddr[2], &macAddr[3], &macAddr[4], &macAddr[5]) != 6){
    Serial.println("La direccion MAC es incorrecta");
  }
  else{
    Serial.print ("La nueva direccion MAC es: ");
    Serial.println(a);
    for (int i=0;i<6;i++)  EEPROM.update (i+6,macAddr[i]);
    Serial.println(); 
    printMenu();
  }  
}

/*
 * Introducir nueva direccion de puerto HTTP
 */
void configuraPuerto(){
  int puerto_;
  EEPROM.get(4,puerto_);
  Serial.print("Puerto HTTP actual = ");
  Serial.println(puerto_);
  Serial.println("Introduce nuevo puerto");
  while (Serial.available() == 0);
  puerto_ = Serial.parseInt();
  Serial.print("El nuevo puerto es: ");
  Serial.println(puerto_);
  EEPROMWriteInt (4,puerto_);
  Serial.println(); 
  printMenu();
}

/*
 * Configura numero de maquina
 */
void configuraNumMaquina(){
  Serial.println("Introduce numero de maquina");
  while (Serial.available() == 0);
  numeroMaquina = Serial.parseInt();
  Serial.print("El nuevo numero de maquina es: ");
  Serial.println(numeroMaquina);
  EEPROMWriteInt (18,numeroMaquina);
  Serial.println(); 
  printMenu();
}

/*
 * Configura modo de respuesta
 *   0 - Respuesta HTML con relojes
 *   1 - Respuesta formato CSV
 *   3 - Respuesta formato XML
 */
void configuraModoRespuesta(){
  byte modoRespuesta__;
  Serial.println("Introduce modo de respuesta: ");
  Serial.println("    0 - Respuesta en HTML con relojes");
  Serial.println("    1 - Respuesta historico de muestras y valor actual en formato CSV");
  Serial.println("    2 - Respuesta historico de muestras y valor actual en formato XML");
  while (Serial.available() == 0);
  modoRespuesta__ = Serial.parseInt();
  if (modoRespuesta__ > 2) {
    Serial.println("La eleccion es incorrecta");
  }
  else {
    modoRespuesta = modoRespuesta__;
    EEPROM.update(20, modoRespuesta);
  }
  printMenu();
}

/*
 * Configurar numero de muestras a guardar
 */
void configuraMuestras(){
  int numeroMuestras_;
  EEPROM.get(12,numeroMuestras_);
  Serial.print("Numero de muestras a guardar = ");
  Serial.println(numeroMuestras_);
  Serial.println("Introduce numero de muestras a guardar (max 256)");
  while (Serial.available() == 0);
  numeroMuestras_ = Serial.parseInt();
  if (numeroMuestras_ > 256) {
    Serial.println("El numero de muestras es incorrecto");
  }
  else{
    Serial.print("El nuevo numero de muestras a guardar es: ");
    Serial.println(numeroMuestras_);
    EEPROMWriteInt (12,numeroMuestras_);
    Serial.println(); 
    borraMuestrasEeprom();
    capturaMuestra();
    printMenu();
  }
}

/*
 * Configurar tiempo entre muestras a guardar
 */
void configuraIntervalo(){
  int muestrasTiempo_;
  EEPROM.get(14,muestrasTiempo_);
  Serial.print("Intervalo en segundos actual entre muestras = ");
  Serial.println(muestrasTiempo_);
  Serial.println("Introduce intervalo en segundos (max 43200 = 12h)");
  while (Serial.available() == 0);
  muestrasTiempo_ = Serial.parseInt();
  if (muestrasTiempo_ > 43200) {
    Serial.println("El intervalo es muy alto");
  }
  else{
    Serial.print("El nuevo intervalo en segundos a guardar es: ");
    Serial.println(muestrasTiempo_);
    EEPROMWriteInt (14,muestrasTiempo_);
    Serial.println(); 
    printMenu();
  }
}

/*
 * Funcion escritura Int16 en EEPROM
 */
void EEPROMWriteInt(int address, int value)
{
  byte two = (value & 0xFF);
  byte one = ((value >> 8) & 0xFF);
  EEPROM.update(address, two);
  EEPROM.update(address + 1, one);
}

/*
 * Inicio de captura de datos al arrancar 
 */
void iniciaCapturaDatos(){
   int intervalo__;
   // Inicia intervalo de capturas 
   EEPROM.get(14,intervalo__);
   Alarm.timerRepeat(intervalo__ , capturaMuestra); 
   // Fin inicializacion capturas
}

/*
 * Funcion de captura de muestras
 */
 void capturaMuestra(){
   tmElements_t timestampCap;
   float temCap;
   float humCap;
   float dewCap;
   int chk = DHT.read22(DHT11_PIN);
   humCap = DHT.humidity;
   temCap = DHT.temperature;
   dewCap = dewPoint(DHT.temperature, DHT.humidity); 
   // Guardar captura
   if (RTC.read(timestampCap)==true){
       guardaCapturaEeprom(temCap, humCap, dewCap, timestampCap);
   
       // Debug
       ponFechaSerie();
       Serial.print("\t");
       Serial.print("T=");
       Serial.print(temCap);
       Serial.print("\tH=");
       Serial.print(humCap);
       Serial.print("\tD=");
       Serial.println(dewCap);
       // Fin debug
   }
   else Serial.print("\nFallo en RTC!! Comprobar bateria y conexiones");
 }

/*
 * Funcion Guardar 14bytes de captura en EEPROM 
 */
void guardaCapturaEeprom(float tem,float hum,float dew,tmElements_t timestamp){
  int puntero_;
  int punteroEEPROM;
  puntero_ = EEPROMReadInt(16); //Cargamos valor puntero muestras
  Serial.print("Puntero eeprom: ");
  Serial.print(puntero_);
  punteroEEPROM = (puntero_ * 14) + 100; // Posicion en EEPROM
  Serial.print ("\t");
  Serial.println (punteroEEPROM);
  EEPROM.update ( punteroEEPROM, timestamp.Day);
  EEPROM.update ( punteroEEPROM+1, timestamp.Month);
  EEPROM.update ( punteroEEPROM+2, tmYearToCalendar(timestamp.Year) -2000);
  EEPROM.update ( punteroEEPROM+3, timestamp.Hour);
  EEPROM.update ( punteroEEPROM+4, timestamp.Minute);
  EEPROM.update ( punteroEEPROM+5, timestamp.Second);
  EEPROMex.writeFloat( punteroEEPROM+6, tem);
  EEPROMex.writeFloat( punteroEEPROM+10, hum);
  puntero_++;
  if (puntero_ ==  numeroMuestras) puntero_ = 0;
  EEPROMWriteInt (16,puntero_);
}

/*
 * Muestra por Serie todos los datos de la Eeprom
 */
void muestraDatosEeprom(){
  int punteroEeprom__;
  int posicionEeprom;
  float tmp, hum;
  int indice = 0;
  punteroEeprom__ = EEPROMReadInt(16); //Cargamos valor del puntero muestras
  if (punteroEeprom__ == numeroMuestras) punteroEeprom__ = 0;
  Serial.println("========================== Muestras capturadas ===========================");
  for (int oo = 0; oo<numeroMuestras; oo++){
      posicionEeprom = (punteroEeprom__ * 14) + 100; // posicion siguiente a la ultima
      if (EEPROM.read(posicionEeprom) > 0)  {
        Serial.print(indice);
        Serial.print("\t");
        indice++;
        print2digits(EEPROM.read(posicionEeprom));
        Serial.print("/");
        print2digits(EEPROM.read(posicionEeprom+1));
        Serial.print("/");
        print2digits(EEPROM.read(posicionEeprom+2));
        Serial.print("\t");
        print2digits(EEPROM.read(posicionEeprom+3));
        Serial.print(":");
        print2digits(EEPROM.read(posicionEeprom+4));
        Serial.print(":");
        print2digits(EEPROM.read(posicionEeprom+5));
        Serial.print("\t");
        Serial.print("T: ");
        tmp = EEPROMex.readFloat(posicionEeprom+6);
        Serial.print(tmp);
        Serial.print("ºC");
        Serial.print("\t");
        Serial.print("H: ");
        hum = EEPROMex.readFloat(posicionEeprom+10);
        Serial.print(hum);
        Serial.print("%");
        Serial.print("\t");
        Serial.print("PdR: ");
        Serial.println(dewPoint(tmp, hum)); 
      }//end if
      punteroEeprom__ ++;
      if (punteroEeprom__ == numeroMuestras) punteroEeprom__ = 0;
  }// end for oo
}

/*
 * Envia datos en XML
 */
 void enviarDatosXML(){
  int punteroEeprom__;
  int posicionEeprom;
  float tmp, hum;
  float temCap;
  float humCap;
  float dewCap;
  byte indice = 0;
  int chk = DHT.read22(DHT11_PIN);
  humCap = DHT.humidity;
  temCap = DHT.temperature;
  dewCap = dewPoint(DHT.temperature, DHT.humidity); 
  punteroEeprom__ = EEPROMReadInt(16); //Cargamos valor del puntero muestras
  if (punteroEeprom__ == numeroMuestras) punteroEeprom__ = 0;
  //Serial.println("Enviando datos XML");
  client.print("<actual>\n  <temperatura>");
  client.print(temCap);
  client.print("</temperatura>\n  <humedad>");
  client.print(humCap);
  client.print("</humedad>\n  <pdr>");
  client.print(dewCap);
  client.print("</pdr>\n</actual>\n");
  for (int oo = 0; oo<numeroMuestras; oo++){
      posicionEeprom = (punteroEeprom__ * 14) + 100; // posicion siguiente a la ultima
      if (EEPROM.read(posicionEeprom) > 0)  {
        client.print("<muestra>\n");
        client.print("  <indice>");
        client.print(indice);
        client.print("</indice>\n  <fecha>");
        indice++;
        send2digits(EEPROM.read(posicionEeprom));
        client.print("/");
        send2digits(EEPROM.read(posicionEeprom+1));
        client.print("/");
        send2digits(EEPROM.read(posicionEeprom+2));
        client.print("</fecha>\n  <hora>");
        send2digits(EEPROM.read(posicionEeprom+3));
        client.print(":");
        send2digits(EEPROM.read(posicionEeprom+4));
        client.print(":");
        send2digits(EEPROM.read(posicionEeprom+5));
        client.print("</hora>\n  <temperatura>");
        tmp = EEPROMex.readFloat(posicionEeprom+6);
        client.print(tmp);
        client.print("</temperatura>\n  <humedad>");
        hum = EEPROMex.readFloat(posicionEeprom+10);
        client.print(hum);
        client.print("</humedad>\n  <pdr>");
        client.print(dewPoint(tmp, hum)); 
        client.print("</pdr>\n");
        client.print("</muestra>\n");
      }//end if
      punteroEeprom__ ++;
      if (punteroEeprom__ == numeroMuestras) punteroEeprom__ = 0;
  }// end for oo
 }

/*
 * Envia datos en formato CSV
 */
void enviarDatosCSV(){
  int punteroEeprom__;
  int posicionEeprom;
  float tmp, hum;
  float temCap;
  float humCap;
  float dewCap;
  byte indice = 0;
  byte resto = 0;
  int chk = DHT.read22(DHT11_PIN);
  
  punteroEeprom__ = EEPROMReadInt(16); //Cargamos valor del puntero muestras
  if (punteroEeprom__ == numeroMuestras) punteroEeprom__ = 0;

  client.print("muestra,timestamp,temperatura,humedad,pdr,maquina\n");
  for (int oo = 0; oo<numeroMuestras; oo++){
      posicionEeprom = (punteroEeprom__ * 14) + 100; // posicion siguiente a la ultima
      if (EEPROM.read(posicionEeprom) > 0)  {
        client.print(indice);
        client.print(",");
        indice++;
        send2digits(EEPROM.read(posicionEeprom));
        client.print("/");
        send2digits(EEPROM.read(posicionEeprom+1));
        client.print("/");
        send2digits(EEPROM.read(posicionEeprom+2));
        client.print(" ");
        send2digits(EEPROM.read(posicionEeprom+3));
        client.print(":");
        send2digits(EEPROM.read(posicionEeprom+4));
        client.print(":");
        send2digits(EEPROM.read(posicionEeprom+5));
        client.print(",");
        tmp = EEPROMex.readFloat(posicionEeprom+6);
        client.print(tmp);
        client.print(",");
        hum = EEPROMex.readFloat(posicionEeprom+10);
        client.print(hum);
        client.print(",");
        client.print(dewPoint(tmp, hum)); 
        client.print(",");
        client.print(numeroMaquina); 
        client.print("\n");
      }//end if
      else resto++;
      punteroEeprom__ ++;
      if (punteroEeprom__ == numeroMuestras) punteroEeprom__ = 0;
  }// end for oo 
  for (int u=0;u<resto;u++){
      client.print(indice);
      client.print(","); 
      send2digits(EEPROM.read(posicionEeprom));
        client.print("/");
        send2digits(EEPROM.read(posicionEeprom+1));
        client.print("/");
        send2digits(EEPROM.read(posicionEeprom+2));
        client.print(" ");
        send2digits(EEPROM.read(posicionEeprom+3));
        client.print(":");
        send2digits(EEPROM.read(posicionEeprom+4));
        client.print(":");
        send2digits(EEPROM.read(posicionEeprom+5));
        client.print(",");
        client.print(tmp);
        client.print(",");
        client.print(hum);
        client.print(",");
        client.print(dewPoint(tmp, hum)); 
        client.print(",");
        client.print(numeroMaquina); 
        client.print("\n");
        indice++;
  }
  //Datos actuales
  humCap = DHT.humidity;
  temCap = DHT.temperature;
  dewCap = dewPoint(DHT.temperature, DHT.humidity); 
  client.print(indice);
  client.print(","); 
  ponFechaTcp();
  client.print(",");
  client.print(temCap);
  client.print(",");
  client.print(humCap);
  client.print(",");
  client.print(dewCap);
  client.print(",");
  client.print(numeroMaquina);
  client.print("\n"); 
      
  
  
}

/*
 * Funcion lectura int16 de EEPROM
 */
int EEPROMReadInt(int address)
{
  long two = EEPROM.read(address);
  long one = EEPROM.read(address + 1);
  return ((two << 0) & 0xFFFFFF) + ((one << 8) & 0xFFFFFFFF);
}

/*
 * Borra muestras de EEPROM
 */
 void borraMuestrasEeprom(){
   //Borrar datos EEPROM de 256 muestras
   for (int iii = 100; iii<3685; iii++){
    EEPROM.update (iii,0);
   }
   // Inicializar puntero de muestras a cero
   EEPROMWriteInt (16,0);
 }
 
/*
 * Poner un cero en digitos <10
 */
void print2digits(int number) {
  if (number >= 0 && number < 10) {
    Serial.print('0');
  }
  Serial.print(number);
}

/*
 * Poner un cero en digitos <10
 */
void send2digits(int number) {
  if (number >= 0 && number < 10) {
    client.print('0');
  }
  client.print(number);
}

/*
 * Poner fecha en contenedor
 */
void ponFechaContenedor(){
  int xxx;
  RTC.read(tm);
  //xxx = tmYearToY2k(tm.Year);
  //tm.Year = xxx;
  T0 = makeTime(tm);
  setTime(T0);
}

/*
 * Poner fecha por Serie
 */
void ponFechaSerie(){
  if (RTC.read(tm)){
    print2digits(tm.Hour);
    Serial.write(':');
    print2digits(tm.Minute);
    Serial.write(':');
    print2digits(tm.Second);
    Serial.print("   ");
    print2digits(tm.Day);
    Serial.write('/');
    print2digits(tm.Month);
    Serial.write('/');
    Serial.print(tmYearToCalendar(tm.Year));
   }
 }

 /*
 * Poner fecha por TCP
 */
void ponFechaTcp(){
  if (RTC.read(tm)){
    send2digits(tm.Day);
    client.print('/');
    send2digits(tm.Month);
    client.print('/');
    send2digits(tmYearToCalendar(tm.Year)-2000);
    client.print(" ");
    send2digits(tm.Hour);
    client.print(':');
    send2digits(tm.Minute);
    client.print(':');
    send2digits(tm.Second);
   }
 }


