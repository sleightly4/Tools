import json
import urllib.request
from html import escape

# Define the URL for the KEV data
kev_url = "https://www.cisa.gov/sites/default/files/feeds/known_exploited_vulnerabilities.json"

# Step 1: Download the KEV file using urllib
def download_kev_file(url):
    try:
        # Fetch the JSON data from the URL
        with urllib.request.urlopen(url) as response:
            kev_data = json.load(response)
        return kev_data
    except Exception as e:
        print(f"Error fetching KEV data: {e}")
        return None

# Step 2: Process the KEV data and extract relevant information
def extract_vulnerabilities(kev_data):
    vulnerabilities = []
    for vuln in kev_data["vulnerabilities"]:
        cve_id = vuln.get("cveID", "N/A")
        description = vuln.get("shortDescription", "No description available")
        date_added = vuln.get("dateAdded", "N/A")
        vulnerabilities.append({"cve": cve_id, "description": description, "date": date_added})
    return vulnerabilities

# Step 3: HTML Makes My Head Hurt(and css)
def generate_html(vulnerabilities, output_file="kev_vulnerabilities.html"):
    html_content = """
    <html>
    <head>
        <title>CISA Known Exploited Vulnerabilities</title>
        <style>
            body {
                background-color: #1e1e1e;
                color: #f1f1f1;
                font-family: Arial, sans-serif;
            }
            h1 {
                text-align: center;
                background-color: #2d6a4f; /* Green background */
                color: #ffffff;
                padding: 20px 0;
                margin: 0;
                font-size: 2.5em;
            }
            table {
                width: 80%;
                margin: 20px auto;
                border-collapse: collapse;
                background-color: #2d2d2d;
                border-radius: 8px;
            }
            th, td {
                padding: 12px;
                text-align: left;
                border-bottom: 1px solid #444;
            }
            th {
                background-color: #1d3d28; /* Dark green */
                color: #f1f1f1;
            }
            tr:nth-child(even) {
                background-color: #333;
            }
            tr:hover {
                background-color: #444;
            }
            td {
                color: #b0b0b0;
            }
            a {
                color: #007bff; /* Blue color for links */
                text-decoration: none;
            }
            a:hover {
                color: #2d6a4f; /* Green color when hovered */
                text-decoration: underline;
            }
        </style>
    </head>
    <body>
        <h1>CISA Known Exploited Vulnerabilities</h1>
        <table>
            <tr>
                <th>CVE</th>
                <th>Description</th>
                <th>Date Added</th>
            </tr>
    """

    # Loop through the vulnerabilities(formatting makes my head hurt)
    for vuln in vulnerabilities:
        cve_link = f"https://nvd.nist.gov/vuln/detail/{vuln['cve']}"
        html_content += f"""
        <tr>
            <td><a href="{escape(cve_link)}" target="_blank">{escape(vuln['cve'])}</a></td>
            <td>{escape(vuln['description'])}</td>
            <td>{escape(vuln['date'])}</td>
        </tr>
        """
    
    html_content += """
        </table>
    </body>
    </html>
    """

    # Write the HTML content to a file
    with open(output_file, "w", encoding="utf-8") as file:
        file.write(html_content)

# Main function that runs it
def main():
    print("Downloading KEV data...")
    kev_data = download_kev_file(kev_url)
    
    if kev_data:
        print("Extracting vulnerabilities...")
        vulnerabilities = extract_vulnerabilities(kev_data)
        
        print("Generating HTML file...")
        generate_html(vulnerabilities)
        print("HTML file 'kev_vulnerabilities.html' generated successfully.")
    else:
        print("Failed to fetch or process KEV data.")

if __name__ == "__main__":
    main()
