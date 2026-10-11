# IOKI Production website

Company website for IOKI Production (Nagaland): wedding films, live telecast and LED setup.
All text, prices, contact details and photos come from the IOKI brochure.

## Files

- `index.html`: the whole site (HTML, CSS and a small menu script in one file)
- `images/`: photos and the logo taken from the brochure

## View it locally

Double-click `index.html` to open it in a browser. No build step is needed.

## Put it online

It is a static site, so any static host works. For example:

- **Netlify Drop**: drag the `ioki-website` folder onto https://app.netlify.com/drop
- **GitHub Pages**: Settings → Pages → deploy from this branch, folder `/ioki-website`
  (GitHub Pages only serves from `/` or `/docs`, so copy the folder to `/docs` first)

## Updating content

- Prices: search `index.html` for `₹`
- Phone, email, Instagram: the `#contact` section near the bottom of `index.html`
- Photos: replace files in `images/` with the same file names
