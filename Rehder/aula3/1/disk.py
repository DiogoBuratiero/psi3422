from PIL import Image, ImageDraw
import math

def gerar_disco_encoder(n_setores=20, tamanho=800, nome_arquivo="disco_encoder.png"):
    """
    Gera um disco PNG com n setores radiais alternados em preto e branco.

    n_setores: número de pulsos (quantos setores preto/branco)
    tamanho: resolução da imagem (px)
    nome_arquivo: nome do arquivo gerado
    """

    # Criar imagem quadrada branca
    img = Image.new("RGB", (tamanho, tamanho), "white")
    draw = ImageDraw.Draw(img)

    # Centro e raio
    cx, cy = tamanho // 2, tamanho // 2
    r = tamanho // 2

    # Ângulo de cada setor
    angulo = 360 / n_setores

    for i in range(n_setores):
        # Seleciona cor alternada
        cor = "black" if i % 2 == 0 else "white"

        # Ângulos do setor i
        inicio = i * angulo
        fim = (i + 1) * angulo

        # Desenha setor (como fatia de pizza)
        draw.pieslice([cx - r, cy - r, cx + r, cy + r],
                      start=inicio,
                      end=fim,
                      fill=cor)

    # Salvar imagem
    img.save(nome_arquivo)
    print(f"Disco gerado: {nome_arquivo}")


# ----- Exemplo de uso -----
if __name__ == "__main__":
    gerar_disco_encoder(n_setores=32, tamanho=800, nome_arquivo="./encoder_32_pulsos.png")
