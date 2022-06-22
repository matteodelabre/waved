const timeline = document.querySelector('.timeline');
const timelineContainer = document.querySelector('.timeline-container');
const timelineItems = timeline.querySelectorAll('[data-start]');
const timelineStart = parseInt(timeline.getAttribute('data-start'));
const timelineEnd = parseInt(timeline.getAttribute('data-end'));
const zoomControl = document.querySelector('#zoom');

let previousZoom = zoomControl.valueAsNumber;

const adjustZoom = (zoom) =>
{
    // Update overall timeline span and scroll position
    const nextWidth = (timelineEnd - timelineStart) * zoom;
    timeline.style.width = `${nextWidth}px`;
    timelineContainer.scrollLeft = (
        timelineContainer.scrollLeft / previousZoom * zoom
    );

    // Update position and span of each item
    for (let item of timelineItems) {
        const start = parseInt(item.getAttribute('data-start'));
        item.style.left = `${(start - timelineStart) * zoom}px`;

        if (item.hasAttribute('data-end')) {
            const end = parseInt(item.getAttribute('data-end'));
            item.style.width = `${(end - start) * zoom}px`;
        }
    }

    previousZoom = zoom;
};

zoomControl.addEventListener('input', () => {
    const zoom = zoomControl.valueAsNumber;
    adjustZoom(zoom);
});
