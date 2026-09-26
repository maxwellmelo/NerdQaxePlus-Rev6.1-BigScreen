#!/usr/bin/env python3
"""Instancia pesos/larguras estaticos a partir do variable font Archivo.

ofl/archivo no repo google/fonts so distribui um unico arquivo variavel
("Archivo[wdth,wght].ttf", eixos wght 100..900 e wdth 62..125). O
lv_font_conv precisa de um TTF estatico (um "corte" fixo dos eixos), entao
este script usa `fontTools.varLib.instancer` para gerar 4 instancias
estaticas, uma por combinacao (peso, largura) realmente usada no design
aprovado das 3 telas-piloto:

  - wght=400 wdth=88  -> rotulos "font:400 ... font-stretch:88%" (19-zen
    unidade/rodape; 01-painel titulo da fita/unidade do hero/rodape)
  - wght=500 wdth=82  -> abas/eixo/medias de 14-hashrate
    ("font:500 ... font-stretch:82%")
  - wght=500 wdth=88  -> frase de insight de 14-hashrade
    ("font:500 17px ... font-stretch:88%")
  - wght=700 wdth=100 -> titulo da tela e valor das medias de 14-hashrate
    ("font:700 ... font-stretch:100%", sem condensamento)

Alternativa considerada e descartada: lv_font_conv nao aceita variable font
direto com selecao de eixo/peso (so le a instancia default do arquivo), por
isso a instanciacao previa e obrigatoria aqui - nao e so uma preferencia.

Requer fonttools (ja usado no ambiente para outras tarefas; instale local
com `python -m pip install --user fonttools` se faltar - nunca globalmente
com privilegios elevados).
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SRC = ROOT / 'ttf' / 'Archivo-VF.ttf'
OUT_DIR = ROOT / 'instanced'

INSTANCES = [
    ('Archivo-Regular-wdth88.ttf', 'wght=400', 'wdth=88'),
    ('Archivo-Medium-wdth82.ttf', 'wght=500', 'wdth=82'),
    ('Archivo-Medium-wdth88.ttf', 'wght=500', 'wdth=88'),
    ('Archivo-Bold-wdth100.ttf', 'wght=700', 'wdth=100'),
]


def main():
    if not SRC.exists():
        sys.exit(f'Nao encontrei {SRC} - rode download-fonts.ps1 primeiro.')
    OUT_DIR.mkdir(exist_ok=True)
    for out_name, wght, wdth in INSTANCES:
        out_path = OUT_DIR / out_name
        cmd = [
            sys.executable, '-m', 'fontTools.varLib.instancer',
            str(SRC), wght, wdth, '-o', str(out_path),
        ]
        print('>', ' '.join(cmd))
        subprocess.run(cmd, check=True)
    print(f'\n{len(INSTANCES)} instancias estaticas geradas em {OUT_DIR}')


if __name__ == '__main__':
    main()
