import csv
from pathlib import Path
import matplotlib.pyplot as plt


def ensure_sample_csv(csv_path: Path) -> None:
    """Создает шаблон CSV-файла с экспериментальными данными, если он отсутствует."""
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    if csv_path.exists():
        return

    sample_rows = [
        {
            "config": "А (10 шт., полный цикл)",
            "objects": 10,
            "run1_delete_ms": 0.42,
            "run2_delete_ms": 0.39,
            "run3_delete_ms": 0.41,
            "run1_restore_ms": 0.68,
            "run2_restore_ms": 0.65,
            "run3_restore_ms": 0.66,
            "deleted_objects": 10,
            "restored_objects": 10,
            "snapshot_kb": 0.85,
        },
        {
            "config": "Б (50 шт., сбой на 25 + откат)",
            "objects": 50,
            "run1_delete_ms": 1.15,
            "run2_delete_ms": 1.12,
            "run3_delete_ms": 1.18,
            "run1_restore_ms": 2.95,
            "run2_restore_ms": 2.88,
            "run3_restore_ms": 2.91,
            "deleted_objects": 25,
            "restored_objects": 50,
            "snapshot_kb": 4.20,
        },
        {
            "config": "В (100 шт., полный цикл)",
            "objects": 100,
            "run1_delete_ms": 3.80,
            "run2_delete_ms": 3.74,
            "run3_delete_ms": 3.85,
            "run1_restore_ms": 5.60,
            "run2_restore_ms": 5.52,
            "run3_restore_ms": 5.58,
            "deleted_objects": 100,
            "restored_objects": 100,
            "snapshot_kb": 8.40,
        },
    ]

    with csv_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(sample_rows[0].keys()))
        writer.writeheader()
        writer.writerows(sample_rows)


def load_experiments(csv_path: Path) -> list[dict]:
    """Загружает результаты экспериментов и вычисляет среднее время (мс)."""
    rows: list[dict] = []
    with csv_path.open("r", newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for raw in reader:
            objects = int(raw["objects"])
            del_times = [
                float(raw["run1_delete_ms"]),
                float(raw["run2_delete_ms"]),
                float(raw["run3_delete_ms"]),
            ]
            rst_times = [
                float(raw["run1_restore_ms"]),
                float(raw["run2_restore_ms"]),
                float(raw["run3_restore_ms"]),
            ]
            rows.append(
                {
                    "config": raw["config"],
                    "objects": objects,
                    "avg_delete_ms": sum(del_times) / len(del_times),
                    "avg_restore_ms": sum(rst_times) / len(rst_times),
                    "deleted_objects": int(raw["deleted_objects"]),
                    "restored_objects": int(raw["restored_objects"]),
                    "snapshot_kb": float(raw["snapshot_kb"]),
                }
            )
    rows.sort(key=lambda r: r["objects"])
    return rows


def plot_time_vs_objects(rows: list[dict], out_path: Path) -> None:
    """График 1: зависимость времени удаления и отката (мс) от числа объектов (шт.)."""
    x_counts = [r["objects"] for r in rows]
    del_ms = [r["avg_delete_ms"] for r in rows]
    rst_ms = [r["avg_restore_ms"] for r in rows]

    plt.figure(figsize=(8, 5))
    plt.plot(x_counts, del_ms, marker="o", linewidth=2, label="Удаление (RegDeleteValueW)")
    plt.plot(x_counts, rst_ms, marker="s", linewidth=2, label="Восстановление (RegSetValueExW)")

    plt.title("Зависимость времени операций от количества объектов реестра (Вариант 12)")
    plt.xlabel("Количество объектов реестра, шт.")
    plt.ylabel("Среднее время выполнения, мс")
    plt.xticks(x_counts)
    plt.grid(True, linestyle="--", alpha=0.6)
    plt.legend()
    plt.tight_layout()
    plt.savefig(out_path, dpi=300)
    plt.close()


def plot_objects_processed(rows: list[dict], out_path: Path) -> None:
    """График 2: удаленные и восстановленные объекты (шт.) по конфигурациям."""
    labels = [r["config"] for r in rows]
    deleted = [r["deleted_objects"] for r in rows]
    restored = [r["restored_objects"] for r in rows]

    x_pos = list(range(len(labels)))
    width = 0.36

    plt.figure(figsize=(9, 5))
    bars_del = plt.bar(
        [p - width / 2 for p in x_pos],
        deleted,
        width=width,
        label="Удалено до завершения / сбоя, шт.",
    )
    bars_rst = plt.bar(
        [p + width / 2 for p in x_pos],
        restored,
        width=width,
        label="Восстановлено при откате, шт.",
    )

    for bar in bars_del:
        h = bar.get_height()
        plt.text(bar.get_x() + bar.get_width() / 2, h + 1.5, f"{int(h)}", ha="center", va="bottom", fontsize=9)
    for bar in bars_rst:
        h = bar.get_height()
        plt.text(bar.get_x() + bar.get_width() / 2, h + 1.5, f"{int(h)}", ha="center", va="bottom", fontsize=9)

    plt.title("Объем удаленных и восстановленных объектов по конфигурациям (Вариант 12)")
    plt.xlabel("Конфигурация эксперимента")
    plt.ylabel("Количество объектов, шт.")
    plt.xticks(x_pos, labels)
    plt.ylim(0, max(restored + [10]) * 1.15)
    plt.grid(True, axis="y", linestyle="--", alpha=0.6)
    plt.legend()
    plt.tight_layout()
    plt.savefig(out_path, dpi=300)
    plt.close()


def main() -> None:
    base_dir = Path(__file__).resolve().parent.parent
    csv_path = base_dir / "data" / "experiments_var12.csv"
    report_dir = base_dir / "report"
    report_dir.mkdir(parents=True, exist_ok=True)

    ensure_sample_csv(csv_path)
    rows = load_experiments(csv_path)

    time_chart = report_dir / "fig1_time_vs_objects.png"
    objs_chart = report_dir / "fig2_deleted_vs_restored.png"

    plot_time_vs_objects(rows, time_chart)
    plot_objects_processed(rows, objs_chart)

    print(f"Данные загружены из: {csv_path}")
    print(f"Сохранен график 1: {time_chart}")
    print(f"Сохранен график 2: {objs_chart}")


if __name__ == "__main__":
    main()