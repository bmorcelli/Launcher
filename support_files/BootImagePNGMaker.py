
from PIL import Image, ImageDraw, ImageFont
import argparse
import random

parser = argparse.ArgumentParser(description="Gera a imagem de boot do Launcher em PNG")
parser.add_argument("--transparent", action="store_true", help="salva com fundo transparente no lugar do BGCOLOR")
parser.add_argument("--scale", type=int, default=1, help="fator inteiro de resolução; 3 gera 1320x675 (padrão: 1)")
args = parser.parse_args()
if args.scale < 1:
    parser.error("--scale deve ser 1 ou maior")

# Configurações
S = args.scale
WIDTH, HEIGHT = 434 * S, 180 * S
# WIDTH, HEIGHT = 440, 354
FGCOLOR = (0, 255, 0)  # Verde
BGCOLOR = (0, 0, 0, 0) if args.transparent else (0, 0, 0)  # Transparente ou preto
even_color = (25, 115, 25)  # Verde escuro
odd_color = (0, 55, 5)    # Verde mais escuro
LW, LH = 9 * S, 15 * S  # Largura e altura de caracteres
NAME_W = 120 * S  # Largura da área do nome

# Fontes
font_small = ImageFont.load_default(15 * S)
font_large = ImageFont.truetype("C:\\Windows\\Fonts\\OCRAEXT.TTF",82 * S)
font_medium = ImageFont.truetype("C:\\Windows\\Fonts\\OCRAEXT.TTF",20 * S)

# Função principal para atualizar frames
def update_frame_with_border(img, do_all=False, var_name=0):
    draw = ImageDraw.Draw(img)
    launcher_text = "Launcher"
    name_options = ["bmorcelli"]
    # name_options = ["@Pirata", "u/bmorcelli", "gh/bmorcelli"]
    name = name_options[var_name]

    x, y = 10 * S, 5 * S

    while y < HEIGHT - 12 * S:
        cor = random.randint(0, 10)
        show = random.randint(0, 39)

        if show == 0 or do_all:
            if cor == 10:
                txt = " "
                color = BGCOLOR
            elif cor % 2 == 0:
                color = even_color
                txt = str(cor)
            else:
                color = odd_color
                txt = str(cor)

            if x + LW > WIDTH - 10 * S:
                x = 10 * S
                y += LH
            elif x < 10 * S:
                x = 10 * S
            if y >= HEIGHT - 12 * S:
                break

            if y + LH > HEIGHT - 25 * S and x + LW > WIDTH - NAME_W and do_all:

                x += LW * 25
            else:
                draw.rectangle((x, y, x + LW - 1, y + LH - 1), fill=BGCOLOR)
                draw.text((x, y), txt, fill=color, font=font_small)
                x += LW
        else:
            if y + LH > HEIGHT - 25 * S and x + LW > WIDTH - NAME_W:
                x += LW * 13
            else:
                x += LW

            if x + LW > WIDTH - 10 * S:
                x = 10 * S
                y += LH

    width = 3 * S

    t_width = draw.textlength(name,font=font_medium)
    draw.rectangle((WIDTH - NAME_W, HEIGHT - 25 * S, WIDTH, HEIGHT - 6 * S), fill=BGCOLOR)
    draw.text((WIDTH - 10 * S - t_width, HEIGHT - 25 * S), name, fill=FGCOLOR, font=font_medium)

    t_width = draw.textlength(launcher_text,font=font_large)
    draw.text((WIDTH // 2 - t_width/2, HEIGHT // 2 - 50 * S), launcher_text, fill=FGCOLOR, font=font_large, stroke_width=3 * S, stroke_fill=(0,50,0))

    draw.rounded_rectangle(
        (width, width, WIDTH - width, HEIGHT - width),
        radius=5 * S,
        outline=FGCOLOR,
        width=width,
    )
    return img

# Gera a imagem com fundo completo e borda
img = Image.new("RGBA" if args.transparent else "RGB", (WIDTH, HEIGHT), BGCOLOR)
img = update_frame_with_border(img, do_all=True)

# Salva o PNG com a borda arredondada
img.save("boot_image.png")
