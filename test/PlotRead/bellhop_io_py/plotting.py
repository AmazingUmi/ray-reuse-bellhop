"""Plotting helpers for Bellhop pressure fields."""

from __future__ import annotations

import numpy as np
from numpy.typing import ArrayLike, NDArray

from .shd import PressureField


def transmission_loss(
    pressure: ArrayLike, *, pressure_floor: float = 1.0e-37
) -> NDArray[np.float64]:
    """Convert complex pressure to positive transmission loss in dB."""
    magnitude = np.abs(np.asarray(pressure, dtype=np.complex128))
    magnitude = np.nan_to_num(
        magnitude, nan=pressure_floor, posinf=pressure_floor, neginf=pressure_floor
    )
    return -20.0 * np.log10(np.maximum(magnitude, pressure_floor))


def plot_field(
    field: PressureField,
    *,
    bearing_index: int = 0,
    source_depth_index: int = 0,
    range_unit: str = "km",
    color_limits: tuple[float, float] | None = None,
    ax=None,
):
    """Plot one bearing/source-depth slice and return ``(figure, axes, artist)``."""
    import matplotlib.pyplot as plt

    if range_unit not in {"m", "km"}:
        raise ValueError("range_unit must be 'm' or 'km'")
    pressure = field.pressure[bearing_index, source_depth_index]
    loss = transmission_loss(pressure)
    ranges = field.header.receiver_ranges_m / (1000.0 if range_unit == "km" else 1.0)
    depths = field.header.receiver_depths_m
    if ax is None:
        figure, ax = plt.subplots(figsize=(9, 5))
    else:
        figure = ax.figure

    if loss.shape[0] > 1 and loss.shape[1] > 1:
        if color_limits is None:
            valid = np.isfinite(np.abs(pressure)) & (np.abs(pressure) > 1.0e-37)
            valid_loss = loss[valid]
            if valid_loss.size:
                upper = 10.0 * np.round(
                    (np.median(valid_loss) + 0.75 * np.std(valid_loss)) / 10.0
                )
                color_limits = (float(upper - 50.0), float(upper))
        artist = ax.pcolormesh(
            ranges,
            depths[: loss.shape[0]],
            loss,
            shading="auto",
            vmin=None if color_limits is None else color_limits[0],
            vmax=None if color_limits is None else color_limits[1],
        )
        artist.set_cmap("viridis_r")
        figure.colorbar(artist, ax=ax, label="Transmission loss (dB)")
        ax.invert_yaxis()
        ax.set_ylabel("Depth (m)")
    elif loss.shape[0] == 1:
        (artist,) = ax.plot(ranges, loss[0], linewidth=1.5)
        ax.invert_yaxis()
        ax.set_ylabel("Transmission loss (dB)")
    else:
        (artist,) = ax.plot(loss[:, 0], depths, linewidth=1.5)
        ax.invert_yaxis()
        ax.invert_xaxis()
        ax.set_xlabel("Transmission loss (dB)")
        ax.set_ylabel("Depth (m)")

    if loss.shape[0] == 1 or loss.shape[1] > 1:
        ax.set_xlabel(f"Range ({range_unit})")
    title = field.header.title.replace("_", " ")
    source_depth = field.header.source_depths_m[source_depth_index]
    ax.set_title(
        f"{title.rstrip()}\n"
        f"Frequency = {field.frequency_hz:g} Hz, source depth = {source_depth:g} m"
    )
    figure.tight_layout()
    return figure, ax, artist
