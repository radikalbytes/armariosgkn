from datetime import datetime
import numpy as np
from mpl_finance import date2num
from matplotlib import style
import matplotlib.pyplot as plt
from mpl_finance import candlestick_ohlc
import matplotlib.dates as md
import matplotlib.ticker as mticker


def grafico_estatico():
    style.use('dark_background')
    plt.rc('font', size=10)
    figura, ax = plt.subplots()
    plt.title('EURUSD', size=15)
    plt.grid(False)

    ohlc = []
    for i in range(len(fecha)):
        agregar = fecha[i], apertura[i], alto[i], bajo[i], cierre[i]
        ohlc.append(agregar)

    # Generar el gráfico ohlc y configuraciones básicas del eje x
    plt.plot(fecha, cierre)
    candlestick_ohlc(ax, ohlc, width=0.0004, colorup='lime', colordown='red')
    xfmt = md.DateFormatter('%d %b %H:%M')
    ax.xaxis.set_major_formatter(xfmt)

    for label in ax.xaxis.get_ticklabels():
        label.set_rotation(90)

    # plt.xticks(fecha)
    ax.xaxis.set_major_locator(mticker.MaxNLocator(25))
    plt.xlim(min(fecha) - 0.001, max(fecha) + 0.0017)
    ax.yaxis.tick_right()
    plt.subplots_adjust(left=0.01, bottom=0.20, right=0.94, top=0.90, wspace=0.2, hspace=0)
    plt.show()

def grafico_interactivo():
    style.use('dark_background')
    plt.rc('font', size=10)
    figura, ax = plt.subplots()
    plt.grid(False)

    ohlc = []
    line = []                          #### AÑADIDO
    fech = []                          #### AÑADIDO
    contador = 0
    for i in range(len(fecha)):
        agregar = fecha[i], apertura[i], alto[i], bajo[i], cierre[i]
        ohlc.append(agregar)
        line.append(cierre[i])         #### AÑADIDO
        fech.append(fecha[i])          #### AÑADIDO
        plt.title('EURUSD', size=15)
        if len(ohlc) <= 20:
            candlestick_ohlc(ax, ohlc, width=0.00025, colorup='lime', colordown='red')
        else:
            candlestick_ohlc(ax, ohlc[-20:], width=0.00025, colorup='lime', colordown='red')
        plt.plot(fech, line)           #### AÑADIDO


        xfmt = md.DateFormatter('%d %b %H:%M')
        ax.xaxis.set_major_formatter(xfmt)
        ax.xaxis.set_major_locator(mticker.MaxNLocator(10))
        ax.yaxis.tick_right()
        plt.pause(0.1)
        if contador < len(fecha) -1:
            plt.cla()
        contador += 1
    plt.show()

# Generar numpy.array con los 4 datos, convirtiendo la fecha en flotante para graficar
convertir = lambda x: date2num(datetime.strptime(x.decode("utf-8"), '%Y%m%d%H%M'))
fecha, apertura, alto, bajo, cierre = np.loadtxt('datos_tratados.csv', delimiter=',', unpack=True,
                                                 converters={0: convertir})

modo = input("Modo del gráfico - e/i: ")
while modo != "e" and modo != "i":
    print("Modo incorrecto")
    modo = input("Modo del gráfico: e/i")

if modo == "e":
    grafico_estatico()
else:
    grafico_interactivo()
